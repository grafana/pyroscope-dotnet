// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "gtest/gtest.h"

#include <map>
#include <optional>
#include <string>

#include "EnvironmentVariables.h"
#include "HttpProxy.h"

namespace
{
std::optional<ProxyConfig> ResolveProxy(const std::string& server, const std::map<shared::WSTRING, std::string>& env)
{
    return HttpProxy::Resolve(Url(server), [&env](const shared::WSTRING& name) {
        auto it = env.find(name);
        return it == env.end() ? std::string{} : it->second;
    });
}
} // namespace

TEST(HttpProxyTest, ResolveReturnsNothingWithoutProxyVariables)
{
    EXPECT_FALSE(ResolveProxy("https://pyroscope.example.com", {}).has_value());
}

TEST(HttpProxyTest, ResolveUsesSchemeSpecificVariable)
{
    const std::map<shared::WSTRING, std::string> env = {
        {WStr("HTTPS_PROXY"), "http://secure-proxy:3128"},
        {WStr("HTTP_PROXY"), "http://plain-proxy:8080"},
    };

    auto httpsProxy = ResolveProxy("https://pyroscope.example.com", env);
    ASSERT_TRUE(httpsProxy.has_value());
    EXPECT_EQ(httpsProxy->host, "secure-proxy");
    EXPECT_EQ(httpsProxy->port, 3128);

    auto httpProxy = ResolveProxy("http://pyroscope.example.com", env);
    ASSERT_TRUE(httpProxy.has_value());
    EXPECT_EQ(httpProxy->host, "plain-proxy");
    EXPECT_EQ(httpProxy->port, 8080);
}

TEST(HttpProxyTest, ResolveUsesLowercaseVariables)
{
    auto proxy = ResolveProxy("https://pyroscope.example.com", {{WStr("https_proxy"), "proxy:3128"}});
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "proxy");
    EXPECT_EQ(proxy->port, 3128);
}

TEST(HttpProxyTest, ResolveParsesCredentialsAndDefaultPort)
{
    auto proxy = ResolveProxy("https://pyroscope.example.com", {{WStr("HTTPS_PROXY"), "http://us%40er:p%3Ass@proxy"}});
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "proxy");
    EXPECT_EQ(proxy->port, 80);
    EXPECT_EQ(proxy->user, "us@er");
    EXPECT_EQ(proxy->password, "p:ss");
}

TEST(HttpProxyTest, ResolveSplitsCredentialsBeforeDecoding)
{
    auto proxy = ResolveProxy("https://pyroscope.example.com", {{WStr("HTTPS_PROXY"), "http://us%3Aer:p%40ss:word@proxy:3128"}});
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->user, "us:er");
    EXPECT_EQ(proxy->password, "p@ss:word");
}

TEST(HttpProxyTest, ResolveMarksIncompleteCredentialsInvalid)
{
    for (const auto* value : {"http://user@proxy:3128", "http://user:@proxy:3128", "http://:secret@proxy:3128", "http://@proxy:3128"})
    {
        auto proxy = ResolveProxy("https://pyroscope.example.com", {{WStr("HTTPS_PROXY"), value}});
        ASSERT_TRUE(proxy.has_value()) << value;
        EXPECT_TRUE(proxy->invalid) << value;
    }
}

TEST(HttpProxyTest, ResolvePrefersPyroscopeProxy)
{
    const std::map<shared::WSTRING, std::string> env = {
        {EnvironmentVariables::PyroscopeProxy, "http://pyroscope-proxy:1080"},
        {WStr("HTTPS_PROXY"), "http://other-proxy:3128"},
        {WStr("NO_PROXY"), "*"},
    };

    auto proxy = ResolveProxy("https://pyroscope.example.com", env);
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "pyroscope-proxy");
    EXPECT_EQ(proxy->port, 1080);
}

TEST(HttpProxyTest, ResolveHonorsNoProxy)
{
    const std::map<shared::WSTRING, std::string> env = {
        {WStr("HTTPS_PROXY"), "http://proxy:3128"},
        {WStr("no_proxy"), "localhost, .internal.example.com"},
    };

    EXPECT_FALSE(ResolveProxy("https://pyroscope.internal.example.com", env).has_value());
    EXPECT_TRUE(ResolveProxy("https://pyroscope.example.com", env).has_value());
}

TEST(HttpProxyTest, ResolveMarksUnsupportedProxiesInvalid)
{
    for (const auto* value : {"https://proxy:3128", "socks5://proxy:1080", "http://proxy:port", "http://proxy:0", "http://proxy:65536"})
    {
        auto proxy = ResolveProxy("https://pyroscope.example.com", {{WStr("HTTPS_PROXY"), value}});
        ASSERT_TRUE(proxy.has_value()) << value;
        EXPECT_TRUE(proxy->invalid) << value;
    }
}

TEST(HttpProxyTest, ResolveIgnoresInvalidProxyWhenBypassed)
{
    const std::map<shared::WSTRING, std::string> env = {
        {WStr("HTTPS_PROXY"), "socks5://proxy:1080"},
        {WStr("NO_PROXY"), "pyroscope.example.com"},
    };

    EXPECT_FALSE(ResolveProxy("https://pyroscope.example.com", env).has_value());
    EXPECT_FALSE(ResolveProxy("https://localhost:4040", {{WStr("HTTPS_PROXY"), "socks5://proxy:1080"}}).has_value());
}

TEST(HttpProxyTest, NoProxyMatchesDomains)
{
    EXPECT_FALSE(HttpProxy::NoProxyMatches("", "example.com", "443"));
    EXPECT_TRUE(HttpProxy::NoProxyMatches("*", "example.com", "443"));
    EXPECT_TRUE(HttpProxy::NoProxyMatches("example.com", "example.com", "443"));
    EXPECT_TRUE(HttpProxy::NoProxyMatches("example.com", "api.example.com", "443"));
    EXPECT_TRUE(HttpProxy::NoProxyMatches(".example.com", "example.com", "443"));
    EXPECT_TRUE(HttpProxy::NoProxyMatches("*.example.com", "api.example.com", "443"));
    EXPECT_TRUE(HttpProxy::NoProxyMatches("other.com, EXAMPLE.com", "example.com", "443"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches("example.com", "badexample.com", "443"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches("api.example.com", "example.com", "443"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches(",,.", "example.com", "443"));
}

TEST(HttpProxyTest, NoProxyMatchesIpAddresses)
{
    EXPECT_TRUE(HttpProxy::NoProxyMatches("127.0.0.1", "127.0.0.1", "443"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches("127.0.0.1", "127.0.0.2", "443"));
    EXPECT_TRUE(HttpProxy::NoProxyMatches("10.0.0.0/8", "10.1.2.3", "443"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches("10.0.0.0/8", "11.1.2.3", "443"));
    EXPECT_TRUE(HttpProxy::NoProxyMatches("192.168.1.0/23", "192.168.0.7", "443"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches("192.168.1.0/24", "192.168.0.7", "443"));
    EXPECT_TRUE(HttpProxy::NoProxyMatches("::1", "::1", "443"));
    EXPECT_TRUE(HttpProxy::NoProxyMatches("[fd00::]/8", "fd12::1", "443"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches("10.0.0.0/8", "::1", "443"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches("10.0.0.0/33", "10.0.0.1", "443"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches("10.0.0.0/8", "example.com", "443"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches("10.0.0.0/8x", "10.0.0.1", "443"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches("10.0.0.0/-8", "10.0.0.1", "443"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches("10.0.0.0/", "10.0.0.1", "443"));
}

TEST(HttpProxyTest, NoProxyMatchesPorts)
{
    EXPECT_TRUE(HttpProxy::NoProxyMatches("example.com:4040", "example.com", "4040"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches("example.com:4040", "example.com", "443"));
    EXPECT_TRUE(HttpProxy::NoProxyMatches(".example.com:4040", "api.example.com", "4040"));
    EXPECT_TRUE(HttpProxy::NoProxyMatches("10.0.0.5:4040", "10.0.0.5", "4040"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches("10.0.0.5:4040", "10.0.0.5", "80"));
    EXPECT_TRUE(HttpProxy::NoProxyMatches("[fd00::1]:4040", "fd00::1", "4040"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches("[fd00::1]:4040", "fd00::1", "80"));
    EXPECT_TRUE(HttpProxy::NoProxyMatches("fd00::1", "fd00::1", "80"));
    EXPECT_FALSE(HttpProxy::NoProxyMatches(":4040", "example.com", "4040"));
}

TEST(HttpProxyTest, ResolveMatchesNoProxyPorts)
{
    const std::map<shared::WSTRING, std::string> env = {
        {WStr("HTTPS_PROXY"), "http://proxy:3128"},
        {WStr("NO_PROXY"), "pyroscope.example.com:4040, other.example.com:443"},
    };

    EXPECT_FALSE(ResolveProxy("https://pyroscope.example.com:4040", env).has_value());
    EXPECT_TRUE(ResolveProxy("https://pyroscope.example.com", env).has_value());
    EXPECT_FALSE(ResolveProxy("https://other.example.com", env).has_value());
}

TEST(HttpProxyTest, ResolveSkipsLoopbackServers)
{
    const std::map<shared::WSTRING, std::string> env = {{WStr("HTTP_PROXY"), "http://proxy:3128"}};

    EXPECT_FALSE(ResolveProxy("http://localhost:4040", env).has_value());
    EXPECT_FALSE(ResolveProxy("http://LOCALHOST:4040", env).has_value());
    EXPECT_FALSE(ResolveProxy("http://127.0.0.1:4040", env).has_value());
    EXPECT_FALSE(ResolveProxy("http://127.1.2.3:4040", env).has_value());
    EXPECT_FALSE(ResolveProxy("http://[::1]:4040", env).has_value());
    EXPECT_FALSE(ResolveProxy("http://[::ffff:127.0.0.1]:4040", env).has_value());
    EXPECT_TRUE(ResolveProxy("http://128.0.0.1:4040", env).has_value());
    EXPECT_TRUE(ResolveProxy("http://localhost.example.com:4040", env).has_value());
}

TEST(HttpProxyTest, ResolveUsesPyroscopeProxyForLoopbackServers)
{
    auto proxy = ResolveProxy("http://localhost:4040", {{EnvironmentVariables::PyroscopeProxy, "http://proxy:3128"}});
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "proxy");
}

TEST(HttpProxyTest, ResolveDoesNotFallBackFromInvalidPyroscopeProxy)
{
    const std::map<shared::WSTRING, std::string> env = {
        {EnvironmentVariables::PyroscopeProxy, "socks5://pyroscope-proxy:1080"},
        {WStr("HTTPS_PROXY"), "http://other-proxy:3128"},
    };

    auto proxy = ResolveProxy("https://pyroscope.example.com", env);
    ASSERT_TRUE(proxy.has_value());
    EXPECT_TRUE(proxy->invalid);
}

TEST(HttpProxyTest, ResolveParsesIpv6Proxy)
{
    auto proxy = ResolveProxy("https://pyroscope.example.com", {{WStr("HTTPS_PROXY"), "http://[fd00::1]:3128"}});
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "fd00::1");
    EXPECT_EQ(proxy->port, 3128);
}

TEST(HttpProxyTest, ResolveTreatsSchemelessServerAsHttp)
{
    const std::map<shared::WSTRING, std::string> env = {
        {WStr("HTTPS_PROXY"), "http://secure-proxy:3128"},
        {WStr("HTTP_PROXY"), "http://plain-proxy:8080"},
    };

    auto proxy = ResolveProxy("//pyroscope.example.com:4040", env);
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "plain-proxy");
}
