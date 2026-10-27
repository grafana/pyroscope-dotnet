//
// Created by root on 11/3/22.
//

#include "PyroscopePprofSink.h"
#include "EnvironmentVariables.h"
#include "Log.h"
#include "OpSysTools.h"
#include "cppcodec/base64_rfc4648.hpp"
#include "dd_profiler_version.h"
#include "gen/push/v1/push.pb.h"
#include "gen/types/v1/types.pb.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/util/json_util.h"
#include "shared/src/native-src/string.h"
#include "shared/src/native-src/util.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <tuple>

namespace
{
constexpr std::string_view LabelScopeName = "otel.scope.name";
constexpr std::string_view LabelScopeVersion = "otel.scope.version";
constexpr std::string_view LabelProcessRuntimeName = "process.runtime.name";
constexpr std::string_view LabelProcessRuntimeVersion = "process.runtime.version";
constexpr std::string_view PyroscopeDotnetScopeName = "com.grafana.pyroscope/dotnet";

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

std::string Trim(const std::string& s)
{
    const auto b = s.find_first_not_of(" \t");
    if (b == std::string::npos)
    {
        return {};
    }
    const auto e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string StripBrackets(const std::string& s)
{
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']')
    {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

size_t ParseIp(const std::string& s, unsigned char (&addr)[16])
{
    if (inet_pton(AF_INET, s.c_str(), addr) == 1)
    {
        return 4;
    }
    if (inet_pton(AF_INET6, s.c_str(), addr) == 1)
    {
        return 16;
    }
    return 0;
}

bool IpMatches(const std::string& entry, const std::string& host)
{
    unsigned char hostAddr[16];
    const auto hostLen = ParseIp(host, hostAddr);
    if (hostLen == 0)
    {
        return false;
    }

    auto ip = entry;
    size_t prefixBits = hostLen * 8;
    const auto slash = entry.find('/');
    if (slash != std::string::npos)
    {
        ip = entry.substr(0, slash);
        const auto bits = entry.substr(slash + 1);
        if (bits.empty() || bits.size() > 3 ||
            !std::all_of(bits.begin(), bits.end(), [](unsigned char c) { return std::isdigit(c); }))
        {
            return false;
        }
        prefixBits = std::stoul(bits);
    }

    unsigned char entryAddr[16];
    if (ParseIp(StripBrackets(ip), entryAddr) != hostLen || prefixBits > hostLen * 8)
    {
        return false;
    }

    const auto fullBytes = prefixBits / 8;
    if (std::memcmp(hostAddr, entryAddr, fullBytes) != 0)
    {
        return false;
    }
    const auto remainingBits = prefixBits % 8;
    if (remainingBits == 0)
    {
        return true;
    }
    const auto mask = static_cast<unsigned char>(0xFF << (8 - remainingBits));
    return (hostAddr[fullBytes] & mask) == (entryAddr[fullBytes] & mask);
}

bool IsIpEntry(const std::string& entry)
{
    unsigned char addr[16];
    return ParseIp(StripBrackets(entry.substr(0, entry.find('/'))), addr) != 0;
}

bool IsLoopback(const std::string& host)
{
    return host == "localhost" ||
           IpMatches("127.0.0.0/8", host) ||
           IpMatches("::1", host) ||
           IpMatches("::ffff:127.0.0.0/104", host);
}

// Bare IPv6 addresses and CIDR ranges have several colons and no port.
std::pair<std::string, std::string> SplitEntryPort(const std::string& entry)
{
    if (entry.front() == '[')
    {
        const auto close = entry.find(']');
        if (close != std::string::npos && close + 1 < entry.size() && entry[close + 1] == ':')
        {
            return {entry.substr(0, close + 1), entry.substr(close + 2)};
        }
        return {entry, {}};
    }
    const auto colon = entry.find(':');
    if (colon != std::string::npos && entry.find(':', colon + 1) == std::string::npos)
    {
        return {entry.substr(0, colon), entry.substr(colon + 1)};
    }
    return {entry, {}};
}

// Url has already validated the percent encoding.
std::string PercentDecode(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '%' && i + 2 < s.size())
        {
            out.push_back(static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        }
        else
        {
            out.push_back(s[i]);
        }
    }
    return out;
}

ProxyConfig ParseProxy(const std::string& value, const std::string& varName)
{
    auto raw = value.find("://") == std::string::npos ? "http://" + value : value;
    try
    {
        Url url(raw);
        if (url.scheme() != "http")
        {
            Log::Error("PyroscopePprofSink: proxy scheme '", url.scheme(), "' in ", varName, " is not supported, only http:// proxies are supported. Profiles will not be uploaded.");
            return ProxyConfig{.invalid = true};
        }
        if (url.host().empty())
        {
            Log::Error("PyroscopePprofSink: proxy host in ", varName, " is empty. Profiles will not be uploaded.");
            return ProxyConfig{.invalid = true};
        }

        ProxyConfig proxy;
        proxy.host = url.host();
        proxy.port = url.port().empty() ? 80 : std::stoi(url.port());
        if (proxy.port == 0)
        {
            Log::Error("PyroscopePprofSink: proxy port in ", varName, " is 0. Profiles will not be uploaded.");
            return ProxyConfig{.invalid = true};
        }
        // Url::user_info() is already percent-decoded, so an encoded ':' in the user
        // name would be taken for the separator. Split the raw user info instead.
        const auto authorityBegin = raw.find("://") + 3;
        const auto authority = raw.substr(authorityBegin, raw.find_first_of("/?#", authorityBegin) - authorityBegin);
        const auto at = authority.rfind('@');
        if (at != std::string::npos)
        {
            const auto userInfo = authority.substr(0, at);
            const auto colon = userInfo.find(':');
            proxy.user = PercentDecode(userInfo.substr(0, colon));
            if (colon != std::string::npos)
            {
                proxy.password = PercentDecode(userInfo.substr(colon + 1));
            }
        }
        return proxy;
    }
    catch (const std::exception&)
    {
        // The exception message has the full URL, which can contain credentials.
        Log::Error("PyroscopePprofSink: invalid proxy URL in ", varName, ". Profiles will not be uploaded.");
        return ProxyConfig{.invalid = true};
    }
}

std::string GetFirstEnv(const std::function<std::string(const shared::WSTRING&)>& getEnv,
                        std::initializer_list<shared::WSTRING> names,
                        std::string& foundName)
{
    for (const auto& name : names)
    {
        auto value = Trim(getEnv(name));
        if (!value.empty())
        {
            foundName = shared::ToString(name);
            return value;
        }
    }
    return {};
}
}

PyroscopePprofSink::PyroscopePprofSink(
    std::string server,
    std::string appName,
    BasicAuth basicAuth,
    PyroscopeTenantId tenantId,
    std::map<std::string, std::string> extraHeaders,
    IRuntimeInfo* runtimeInfo,
    const std::vector<std::pair<std::string, std::string>>& staticTags) :
    _appName(appName),
    _runtimeInfo(runtimeInfo),
    _staticTags(staticTags),
    _url(server),
    _basicAuth(std::move(basicAuth)),
    _tenantId(std::move(tenantId)),
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

    auto proxy = ResolveProxy(_url, [](const shared::WSTRING& name) {
        return shared::ToString(shared::GetEnvironmentValue(name));
    });
    if (proxy && proxy->invalid)
    {
        _uploadDisabled = true;
    }
    else if (proxy)
    {
        Log::Info("PyroscopePprofSink: using proxy ", proxy->host, ":", proxy->port);
        _client.set_proxy(proxy->host, proxy->port);
        if (!proxy->user.empty())
        {
            _client.set_proxy_basic_auth(proxy->user, proxy->password);
        }
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
    if (_uploadDisabled)
    {
        return;
    }
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

void PyroscopePprofSink::SetBasicAuth(BasicAuth basicAuth)
{
    std::lock_guard<std::mutex> auth_guard(_authLock);
    _basicAuth = std::move(basicAuth);
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

    AddLabel(series, LabelScopeName, PyroscopeDotnetScopeName);
    AddLabel(series, LabelScopeVersion, PROFILER_VERSION);
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
    if (!_basicAuth.user.empty() && !_basicAuth.password.empty())
    {
        headers.emplace("Authorization", "Basic " + cppcodec::base64_rfc4648::encode(_basicAuth.user + ":" + _basicAuth.password));
    }
    else if (!_url.user_info().empty())
    {
        headers.emplace("Authorization", "Basic " + cppcodec::base64_rfc4648::encode(_url.user_info()));
    }
    if (!_tenantId.value.empty())
    {
        headers.emplace("X-Scope-OrgID", _tenantId.value);
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


std::optional<ProxyConfig> PyroscopePprofSink::ResolveProxy(const Url& server, const std::function<std::string(const shared::WSTRING&)>& getEnv)
{
    std::string varName;
    auto value = GetFirstEnv(getEnv, {EnvironmentVariables::PyroscopeProxy}, varName);
    if (!value.empty())
    {
        return ParseProxy(value, varName);
    }

    if (server.scheme() == "https")
    {
        value = GetFirstEnv(getEnv, {WStr("HTTPS_PROXY"), WStr("https_proxy")}, varName);
    }
    else
    {
        value = GetFirstEnv(getEnv, {WStr("HTTP_PROXY"), WStr("http_proxy")}, varName);
    }
    if (value.empty() || IsLoopback(ToLower(StripBrackets(server.host()))))
    {
        return std::nullopt;
    }

    std::string noProxyName;
    auto noProxy = GetFirstEnv(getEnv, {WStr("NO_PROXY"), WStr("no_proxy")}, noProxyName);
    auto port = server.port();
    if (port.empty())
    {
        port = server.scheme() == "https" ? "443" : "80";
    }
    if (NoProxyMatches(noProxy, server.host(), port))
    {
        return std::nullopt;
    }
    return ParseProxy(value, varName);
}

bool PyroscopePprofSink::NoProxyMatches(const std::string& noProxy, const std::string& host, const std::string& port)
{
    const auto lowerHost = ToLower(StripBrackets(host));
    std::stringstream entries(noProxy);
    std::string entry;
    while (std::getline(entries, entry, ','))
    {
        entry = ToLower(Trim(entry));
        if (entry.empty())
        {
            continue;
        }
        if (entry == "*")
        {
            return true;
        }
        std::string entryPort;
        std::tie(entry, entryPort) = SplitEntryPort(entry);
        if (entry.empty() || (!entryPort.empty() && entryPort != port))
        {
            continue;
        }
        if (IsIpEntry(entry))
        {
            if (IpMatches(entry, lowerHost))
            {
                return true;
            }
            continue;
        }

        if (entry.rfind("*.", 0) == 0)
        {
            entry = entry.substr(1);
        }
        if (entry.front() == '.')
        {
            entry = entry.substr(1);
        }
        if (entry.empty())
        {
            continue;
        }
        if (lowerHost == entry ||
            (lowerHost.size() > entry.size() &&
             lowerHost.compare(lowerHost.size() - entry.size(), entry.size(), entry) == 0 &&
             lowerHost[lowerHost.size() - entry.size() - 1] == '.'))
        {
            return true;
        }
    }
    return false;
}

std::map<std::string, std::string> PyroscopePprofSink::ParseHeadersJSON(std::string headers)
{
    std::map<std::string, std::string> result;
    if (headers.empty())
    {
        return result;
    }

    google::protobuf::Struct json;
    auto status = google::protobuf::util::JsonStringToMessage(headers, &json);
    if (!status.ok())
    {
        Log::Error("PyroscopePprofSink: failed to parse headers json: ", status.ToString());
        return result;
    }

    for (const auto& [key, value] : json.fields())
    {
        if (value.kind_case() == google::protobuf::Value::kStringValue)
        {
            result[key] = value.string_value();
        }
        else
        {
            Log::Error("PyroscopePprofSink: header value is not a string: ", key);
        }
    }

    return result;
}
