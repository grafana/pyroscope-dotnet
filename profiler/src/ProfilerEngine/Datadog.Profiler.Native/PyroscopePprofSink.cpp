//
// Created by root on 11/3/22.
//

#include "PyroscopePprofSink.h"
#include "Log.h"
#include "OpSysTools.h"
#include "cppcodec/base64_rfc4648.hpp"
#include "nlohmann/json.hpp"
#include "gen/push/v1/push.pb.h"
#include "gen/types/v1/types.pb.h"

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
    _staticTags(staticTags),
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
        .pprofs = {},
        .startTime = ProfileTime(),
        .endTime = ProfileTime(),
    });
    _workerThread.join();
}

void PyroscopePprofSink::Export(std::vector<Pprof> pprofs, ProfileTime& startTime, ProfileTime& endTime)
{
    if (_queue.size() >= 3)
    {
        Log::Warn("PyroscopePprofSink queue is too big. Dropping pprof data.");
        return;
    }
    PyroscopeRequest req{
        .pprofs = std::move(pprofs),
        .startTime = startTime,
        .endTime = endTime,
    };
    _queue.push(std::move(req));
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
        for (auto& pprof : req.pprofs)
        {
            upload(std::move(pprof), req.startTime, req.endTime);
        }
    }
}

void PyroscopePprofSink::upload(Pprof pprof, ProfileTime& startTime, ProfileTime& endTime)
{
    push::v1::PushRequest request;
    auto* series = request.add_series();

    auto* nameLabel = series->add_labels();
    nameLabel->set_name("__name__");
    nameLabel->set_value("process_cpu");

    auto* serviceLabel = series->add_labels();
    serviceLabel->set_name("service_name");
    serviceLabel->set_value(_appName);

    auto* spyLabel = series->add_labels();
    spyLabel->set_name("spy_name");
    spyLabel->set_value("dotnetspy");

    for (const auto& tag : _staticTags)
    {
        auto* label = series->add_labels();
        label->set_name(tag.first);
        label->set_value(tag.second);
    }

    auto* sample = series->add_samples();
    sample->set_raw_profile(std::move(pprof));

    std::string body = request.SerializeAsString();
    std::string path = _url.path() + "/push.v1.PusherService/Push";

    httplib::Headers headers = getHeaders();
    auto res = _client.Post(path, headers, body, "application/proto");
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
    headers.emplace("User-Agent", "pyroscope-dotnet/" PYROSCOPE_SPY_VERSION " cpp-httplib");
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
