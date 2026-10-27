#include "HttpProxy.h"
#include "EnvironmentVariables.h"
#include "Log.h"

#ifdef _WINDOWS
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <tuple>

namespace
{
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

std::optional<ProxyConfig> HttpProxy::Resolve(const Url& server, const std::function<std::string(const shared::WSTRING&)>& getEnv)
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

bool HttpProxy::NoProxyMatches(const std::string& noProxy, const std::string& host, const std::string& port)
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
