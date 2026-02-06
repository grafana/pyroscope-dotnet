//
// Created by root on 11/3/22.
//

#include "PyroscopePprofSink.h"
#include "Log.h"
#include "OpSysTools.h"
#include "Sample.h"
#include "cppcodec/base64_rfc4648.hpp"
#include "nlohmann/json.hpp"

PyroscopePprofSink::PyroscopePprofSink(
    std::string server,
    std::string appName,
    std::string authToken,
    std::string basicAuthUser,
    std::string basicAuthPassword,
    std::string tenantID,
    std::map<std::string, std::string> extraHeaders,
    const std::vector<std::pair<std::string, std::string>>& staticTags) :
    _appName(appName),
    _appNameWithLabels(AppNameWithLabels(appName, staticTags)),
    _url(server),
    _authToken(authToken),
    _basicAuthUser(basicAuthUser),
    _basicAuthPassword(basicAuthPassword),
    _tenantID(tenantID),
    _extraHeaders(extraHeaders),
    _client(SchemeHostPort(_url)),
    _running(true)
{
    _client.set_connection_timeout(10);
    _client.set_read_timeout(10);

    _workerThread = std::thread(&PyroscopePprofSink::work, this);
}

PyroscopePprofSink::~PyroscopePprofSink()
{
    _running.store(false);
    _queue.push(PyroscopeRequest{
        .pprof = "",
        .type = ProfileType::CPU,
        .startTime = ProfileTime(),
        .endTime = ProfileTime(),
    });
    _workerThread.join();
}

void PyroscopePprofSink::Export(Pprof pprof, ProfileType type, const ProfileTime& startTime, const ProfileTime& endTime)
{
    if (_queue.size() >= 8)
    {
        Log::Warn("PyroscopePprofSink queue is too big. Dropping pprof data.");
        return;
    }
    PyroscopeRequest req{
        .pprof = std::move(pprof),
        .type = type,
        .startTime = startTime,
        .endTime = endTime,
    };
    _queue.push(req);
}

void PyroscopePprofSink::SetAuthToken(std::string authToken)
{
    std::lock_guard<std::mutex> auth_guard(_authLock);
    _authToken = authToken;
    _basicAuthUser = "";
    _basicAuthPassword = "";
}

void PyroscopePprofSink::SetBasicAuth(std::string user, std::string password)
{
    std::lock_guard<std::mutex> auth_guard(_authLock);
    _basicAuthUser = user;
    _basicAuthPassword = password;
    _authToken = "";
}

void PyroscopePprofSink::work()
{
    OpSysTools::SetNativeThreadName(WStr("Pyroscope.Pprof.Uploader"));
    while (_running.load())
    {
        PyroscopeRequest req = {};
        _queue.waitAndPop(req);
        if (req.pprof.empty())
        {
            continue;
        }
        upload(std::move(req.pprof), req.startTime, req.endTime);
    }
}

void PyroscopePprofSink::upload(Pprof pprof, ProfileTime& startTime, ProfileTime& endTime)
{
    httplib::MultipartFormDataItems data;
    data.emplace_back(httplib::MultipartFormData{
        .name = "profile",
        .content = std::move(pprof),
        .filename = "profile.pprof",
    });

    std::string path = Url()
                           .path(_url.path() + "/ingest")
                           .add_query("name", _appNameWithLabels)
                           .add_query("from", std::to_string(startTime.time_since_epoch().count()))
                           .add_query("until", std::to_string(endTime.time_since_epoch().count()))
                           .add_query("spyName", "dotnetspy")
                           .add_query("spyVersion", PYROSCOPE_SPY_VERSION)
                           .str();

    httplib::Headers headers = getHeaders();
    auto res = _client.Post(path, headers, data);
    if (res)
    {
        Log::Debug("PyroscopePprofSink ", res->status);
    }
    else
    {
        auto err = res.error();
        Log::Error("PyroscopePprofSink err ", to_string(err), " ", _url);
    }
}

httplib::Headers PyroscopePprofSink::getHeaders()
{
    httplib::Headers headers;
    std::lock_guard<std::mutex> auth_guard(_authLock);
    if (!_authToken.empty())
    {
        headers.emplace("Authorization", "Bearer " + _authToken);
    }
    else if (!_basicAuthUser.empty() && !_basicAuthPassword.empty())
    {
        headers.emplace("Authorization", "Basic " + cppcodec::base64_rfc4648::encode(_basicAuthUser + ":" + _basicAuthPassword));
    }
    else if (!_url.user_info().empty())
    {
        headers.emplace("Authorization", "Basic " + cppcodec::base64_rfc4648::encode(_url.user_info()));
    }
    if (!_tenantID.empty())
    {
        headers.emplace("X-Scope-OrgID", _tenantID);
    }
    for (const auto& item : _extraHeaders)
    {
        headers.emplace(item.first, item.second);
    }
    return headers;
}

std::string PyroscopePprofSink::SchemeHostPort(Url& url)
{
    std::stringstream url_builder;
    if (url.scheme().empty())
    {
        Log::Error("PyroscopePprofSink: url scheme empty, falling back to http", url.str());
        url_builder << "http";
    } else {
        url_builder << url.scheme();
    }
    url_builder << "://";
    if (url.host().empty())
    {
        Log::Error("PyroscopePprofSink: url host empty, falling back to localhost", url.str());
        url_builder << "localhost";
    } else {
        url_builder << url.host();
    }
    if (!url.port().empty())
    {
        url_builder << ":" << url.port();
    }
    auto res = url_builder.str();
//    std::cout << "Scheme host port .>> " << res << std::endl;
    return res;
}

std::string PyroscopePprofSink::AppNameWithLabels(const std::string& appName, const std::vector<std::pair<std::string, std::string>>& staticTags)
{
    std::stringstream app_name_builder;
    app_name_builder << appName << "{";
    int i = 0;
    for (const auto& item : staticTags)
    {
        if (i != 0)
        {
            app_name_builder << ",";
        }
        app_name_builder << item.first << "=" << item.second;
        i++;
    }
    app_name_builder << "}";
    return app_name_builder.str();
}

std::map<std::string, std::string> PyroscopePprofSink::ParseHeadersJSON(std::string headers)
{
    std::map<std::string, std::string> result;
    if (headers.empty())
    {
        return result;
    }
    try
    {
        nlohmann::json json = nlohmann::json::parse(headers);
        if (json.is_object())
        {
            for (auto it = json.begin(); it != json.end(); ++it)
            {
                if (it.value().is_string())
                {
                    result[it.key()] = it.value();
                }
                else
                {
                    Log::Error("PyroscopePprofSink: header value is not a string: ", it.key(), " ", it.value());
                }
            }
        }
        else
        {
            Log::Error("PyroscopePprofSink: headers is not a JSON object");
        }
    }
    catch (...)
    {
        Log::Error("PyroscopePprofSink: failed to parse headers json", headers);
    }
    return result;
}
