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
#include "shared/src/native-src/string.h"
#include "shared/src/native-src/util.h"

namespace
{
constexpr std::string_view LabelScopeName = "otel.scope.name";
constexpr std::string_view LabelScopeVersion = "otel.scope.version";
constexpr std::string_view LabelProcessRuntimeName = "process.runtime.name";
constexpr std::string_view LabelProcessRuntimeVersion = "process.runtime.version";

std::string BuildPushPath(const Url& url)
{
    Url pushUrl;
    pushUrl.path(url.path() + "/push.v1.PusherService/Push");
    return pushUrl.str();
}

void AddLabel(push::v1::RawProfileSeries* series, std::string_view name, std::string_view value)
{
    auto* label = series->add_labels();
    label->set_name(std::string(name));
    label->set_value(std::string(value));
}
}

PyroscopePprofSink::PyroscopePprofSink(
    std::string server,
    std::string appName,
    std::string authToken,
    std::string basicAuthUser,
    std::string basicAuthPassword,
    std::string tenantID,
    std::map<std::string, std::string> extraHeaders,
    PyroscopeSemanticLabels semanticLabels,
    IRuntimeInfo* runtimeInfo,
    const std::vector<std::pair<std::string, std::string>>& staticTags) :
    _appName(appName),
    _semanticLabels(std::move(semanticLabels)),
    _runtimeInfo(runtimeInfo),
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

    // Honor SSL_CERT_FILE for a custom/private CA. OpenSSL consults it via the
    // default verify paths only on Unix (cpp-httplib's load_system_certs takes
    // the Windows cert-store branch and never calls SSL_CTX_set_default_verify_paths),
    // so wire it explicitly here for parity. Setting a CA path also makes
    // cpp-httplib verify with OpenSSL alone, skipping its extra Windows Schannel
    // check that wouldn't know about a privately-trusted CA.
    auto caCertFile = shared::ToString(shared::GetEnvironmentValue(WStr("SSL_CERT_FILE")));
    if (!caCertFile.empty())
    {
        _client.set_ca_cert_path(caCertFile);
    }

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    // OpenSSL 3.x flags a TLS connection closed without a close_notify alert as a
    // fatal read error (SSL_R_UNEXPECTED_EOF_WHILE_READING) rather than a clean EOF.
    // When a remote server closes the upload connection that way, every successful
    // upload still fails to read the response: it logs a spurious "Failed to read
    // connection" and the real HTTP status is never seen. Treat the unclean shutdown
    // as a normal end-of-stream. tls_context() returns null for plain-HTTP clients,
    // so this is a no-op there.
    if (auto* sslCtx = static_cast<SSL_CTX*>(_client.tls_context()))
    {
        SSL_CTX_set_options(sslCtx, SSL_OP_IGNORE_UNEXPECTED_EOF);
    }
#endif

    _workerThread = std::thread(&PyroscopePprofSink::work, this);
}

PyroscopePprofSink::~PyroscopePprofSink()
{
    _running.store(false);
    _queue.push(PyroscopeRequest{
        .pprofs = {},
    });
    _workerThread.join();
}

void PyroscopePprofSink::Export(std::vector<Pprof> pprofs)
{
    if (_queue.size() >= 3)
    {
        Log::Warn("PyroscopePprofSink queue is too big. Dropping pprof data.");
        return;
    }
    PyroscopeRequest req{
        .pprofs = std::move(pprofs),
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
            upload(std::move(pprof));
        }
    }
}

static std::string_view ProfileTypeName(ProfileType type)
{
    switch (type)
    {
        case ProfileType::ProcessCpu:     return "process_cpu";
        case ProfileType::CpuSample:      return "process_cpu";
        case ProfileType::GcThreadsCpu:   return "gc";
        case ProfileType::WallTime:       return "wall";
        case ProfileType::Alloc:          return "memory";
        case ProfileType::AllocFramework: return "memory";
        case ProfileType::Lock:           return "lock";
        case ProfileType::Exception:      return "exception";
        case ProfileType::Network:        return "network";
        case ProfileType::Heap:           return "memory";
        case ProfileType::GcCpu:          return "gc";
        case ProfileType::GcStw:          return "gc";
        case ProfileType::ThreadLifetime: return "thread_lifetime";
        default:                          return "unknown";
    }
}

void PyroscopePprofSink::upload(Pprof pprof)
{
    push::v1::PushRequest request;
    auto* series = request.add_series();

    auto* nameLabel = series->add_labels();
    nameLabel->set_name("__name__");
    nameLabel->set_value(std::string(ProfileTypeName(pprof.profileType)));

    auto* serviceLabel = series->add_labels();
    serviceLabel->set_name("service_name");
    serviceLabel->set_value(_appName);

    auto* spyLabel = series->add_labels();
    spyLabel->set_name("spy_name");
    spyLabel->set_value("dotnetspy");

    AddLabel(series, LabelScopeName, _semanticLabels.ScopeName);
    AddLabel(series, LabelScopeVersion, _semanticLabels.ScopeVersion);
    AddLabel(series, LabelProcessRuntimeName, _runtimeInfo->GetRuntimeName());
    AddLabel(series, LabelProcessRuntimeVersion, _runtimeInfo->GetRuntimeVersion());

    for (const auto& tag : _staticTags)
    {
        auto* label = series->add_labels();
        label->set_name(tag.first);
        label->set_value(tag.second);
    }

    auto* sample = series->add_samples();
    sample->set_raw_profile(std::move(pprof.bytes));

    std::string body = request.SerializeAsString();
    std::string path = BuildPushPath(_url);

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
