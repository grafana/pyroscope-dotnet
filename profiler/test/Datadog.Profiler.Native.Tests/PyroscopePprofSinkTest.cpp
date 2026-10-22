// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "gtest/gtest.h"

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cppcodec/base64_rfc4648.hpp"
#include "EnvironmentHelper.h"
#include "EnvironmentVariables.h"
#include "PyroscopePprofSink.h"
#include "RuntimeInfo.h"
#include "dd_profiler_version.h"
#include "gen/push/v1/push.pb.h"

using namespace std::chrono_literals;

namespace
{
struct ObservedRequest
{
    void Set(std::string requestPath,
             int responseStatus,
             std::string requestBody = {},
             std::string authorization = {},
             std::string tenantId = {})
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (Seen)
        {
            return;
        }

        Path = std::move(requestPath);
        Status = responseStatus;
        Body = std::move(requestBody);
        Authorization = std::move(authorization);
        TenantID = std::move(tenantId);
        Seen = true;
        Cv.notify_one();
    }

    bool WaitForRequest(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(Mutex);
        return Cv.wait_for(lock, timeout, [this] { return Seen; });
    }

    std::string Path;
    std::string Body;
    int Status = -1;
    std::string Authorization;
    std::string TenantID;

private:
    std::mutex Mutex;
    std::condition_variable Cv;
    bool Seen = false;
};

std::map<std::string, std::string> GetSeriesLabels(const push::v1::RawProfileSeries& series)
{
    std::map<std::string, std::string> labels;
    for (const auto& label : series.labels())
    {
        labels[label.name()] = label.value();
    }

    return labels;
}

void ValidatePushPath(const std::string& serverPath, const std::string& expectedPath)
{
    httplib::Server server;
    ObservedRequest observed;

    server.Post(expectedPath, [](const httplib::Request&, httplib::Response& res) { res.status = 200; });
    server.set_logger([&observed](const httplib::Request& req, const httplib::Response& res) {
        observed.Set(req.path, res.status, req.body);
    });

    auto port = server.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);

    std::thread serverThread([&server]() {
        server.listen_after_bind();
    });
    server.wait_until_ready();

    {
        const auto url = "http://127.0.0.1:" + std::to_string(port) + serverPath;
        RuntimeInfo runtimeInfo(8, 0, false);
        PyroscopePprofSink sink(
            url, "service", BasicAuth{}, PyroscopeTenantId{}, {}, &runtimeInfo, {});

        Pprof pprof;
        pprof.bytes = "fake-pprof";
        pprof.profileType = ProfileType::ProcessCpu;

        sink.Export({std::move(pprof)});

        const bool requestReceived = observed.WaitForRequest(2s);
        EXPECT_TRUE(requestReceived);
        if (requestReceived)
        {
            EXPECT_EQ(observed.Path, expectedPath);
            EXPECT_EQ(observed.Status, 200);
        }
    }

    server.stop();
    serverThread.join();
}

TEST(PyroscopePprofSinkTest, UploadDoesNotOverrideUserProvidedSemanticLabels)
{
    httplib::Server server;
    ObservedRequest observed;
    const std::string expectedPath = "/push.v1.PusherService/Push";

    server.Post(expectedPath, [](const httplib::Request&, httplib::Response& res) { res.status = 200; });
    server.set_logger([&observed](const httplib::Request& req, const httplib::Response& res) {
        observed.Set(req.path, res.status, req.body);
    });

    auto port = server.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);

    std::thread serverThread([&server]() {
        server.listen_after_bind();
    });
    server.wait_until_ready();

    {
        const auto url = "http://127.0.0.1:" + std::to_string(port);
        RuntimeInfo runtimeInfo(8, 0, false);
        PyroscopePprofSink sink(
            url,
            "service",
            BasicAuth{},
            PyroscopeTenantId{},
            {},
            &runtimeInfo,
            {
                {"otel.scope.name", "user.scope"},
                {"otel.scope.version", "user-version"},
                {"process.runtime.name", "user-runtime"},
                {"process.runtime.version", "user-runtime-version"},
            });

        Pprof pprof;
        pprof.bytes = "fake-pprof";
        pprof.profileType = ProfileType::ProcessCpu;

        sink.Export({std::move(pprof)});

        ASSERT_TRUE(observed.WaitForRequest(2s));

        push::v1::PushRequest request;
        ASSERT_TRUE(request.ParseFromString(observed.Body));
        ASSERT_EQ(request.series_size(), 1);

        const auto labels = GetSeriesLabels(request.series(0));
        EXPECT_EQ(labels.at("otel.scope.name"), "user.scope");
        EXPECT_EQ(labels.at("otel.scope.version"), "user-version");
        EXPECT_EQ(labels.at("process.runtime.name"), "user-runtime");
        EXPECT_EQ(labels.at("process.runtime.version"), "user-runtime-version");
    }

    server.stop();
    serverThread.join();
}

TEST(PyroscopePprofSinkTest, ParseHeadersJSONReturnsStringHeaders)
{
    const auto headers =
        PyroscopePprofSink::ParseHeadersJSON(R"({"Authorization":"Bearer token","X-Scope-OrgID":"tenant"})");

    const std::map<std::string, std::string> expected = {
        {"Authorization", "Bearer token"},
        {"X-Scope-OrgID", "tenant"},
    };
    EXPECT_EQ(headers, expected);
}

TEST(PyroscopePprofSinkTest, ParseHeadersJSONIgnoresNonStringValues)
{
    const auto headers = PyroscopePprofSink::ParseHeadersJSON(
        R"({"valid":"value","number":42,"boolean":true,"null":null,"object":{},"array":[]})");

    const std::map<std::string, std::string> expected = {{"valid", "value"}};
    EXPECT_EQ(headers, expected);
}

TEST(PyroscopePprofSinkTest, ParseHeadersJSONReturnsEmptyMapForInvalidInput)
{
    EXPECT_TRUE(PyroscopePprofSink::ParseHeadersJSON("").empty());
    EXPECT_TRUE(PyroscopePprofSink::ParseHeadersJSON("{invalid").empty());
    EXPECT_TRUE(PyroscopePprofSink::ParseHeadersJSON(R"(["not","an","object"])").empty());
}
} // namespace

TEST(PyroscopePprofSinkTest, UploadUsesNormalizedPathWhenBasePathHasTrailingSlash)
{
    ValidatePushPath("/tenant/", "/tenant/push.v1.PusherService/Push");
}

TEST(PyroscopePprofSinkTest, UploadUsesNormalizedPathWhenBasePathContainsDotSegments)
{
    ValidatePushPath("/tenant/./profiles/../", "/tenant/push.v1.PusherService/Push");
}

TEST(PyroscopePprofSinkTest, UploadUsesNormalizedPathForNestedPaths)
{
    ValidatePushPath("/api/profiles/", "/api/profiles/push.v1.PusherService/Push");
}

TEST(PyroscopePprofSinkTest, UploadUsesBasicAuthAndTenantConstructorArguments)
{
    httplib::Server server;
    ObservedRequest observed;
    const std::string expectedPath = "/tenant/push.v1.PusherService/Push";

    server.Post(expectedPath, [](const httplib::Request&, httplib::Response& res) { res.status = 200; });
    server.set_logger([&observed](const httplib::Request& req, const httplib::Response& res) {
        observed.Set(
            req.path,
            res.status,
            req.body,
            req.get_header_value("Authorization"),
            req.get_header_value("X-Scope-OrgID"));
    });

    auto port = server.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);

    std::thread serverThread([&server]() {
        server.listen_after_bind();
    });
    server.wait_until_ready();

    {
        const auto url = "http://127.0.0.1:" + std::to_string(port) + "/tenant/";
        RuntimeInfo runtimeInfo(8, 0, false);
        PyroscopePprofSink sink(
            url,
            "service",
            BasicAuth{"pyroscope-user", "pyroscope-password"},
            PyroscopeTenantId{"pyroscope-tenant"},
            {},
            &runtimeInfo,
            {});

        Pprof pprof;
        pprof.bytes = "fake-pprof";
        pprof.profileType = ProfileType::ProcessCpu;

        sink.Export({std::move(pprof)});

        const bool requestReceived = observed.WaitForRequest(2s);
        ASSERT_TRUE(requestReceived);

        EXPECT_EQ(observed.Path, expectedPath);
        EXPECT_EQ(observed.Status, 200);
        EXPECT_EQ(
            observed.Authorization,
            "Basic " + cppcodec::base64_rfc4648::encode(std::string{"pyroscope-user:pyroscope-password"}));
        EXPECT_EQ(observed.TenantID, "pyroscope-tenant");
    }

    server.stop();
    serverThread.join();
}

TEST(PyroscopePprofSinkTest, UploadUsesUpdatedDotnetFrameworkRuntimeVersion)
{
    httplib::Server server;
    ObservedRequest observed;
    const std::string expectedPath = "/push.v1.PusherService/Push";

    server.Post(expectedPath, [](const httplib::Request&, httplib::Response& res) { res.status = 200; });
    server.set_logger([&observed](const httplib::Request& req, const httplib::Response& res) {
        observed.Set(req.path, res.status, req.body);
    });

    auto port = server.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);

    std::thread serverThread([&server]() {
        server.listen_after_bind();
    });
    server.wait_until_ready();

    {
        const auto url = "http://127.0.0.1:" + std::to_string(port);
        RuntimeInfo runtimeInfo(4, 0, true);
        PyroscopePprofSink sink(
            url, "service", BasicAuth{}, PyroscopeTenantId{}, {}, &runtimeInfo, {});

        runtimeInfo.SetMinorVersions(8, 9195, 0);

        Pprof pprof;
        pprof.bytes = "fake-pprof";
        pprof.profileType = ProfileType::ProcessCpu;

        sink.Export({std::move(pprof)});

        ASSERT_TRUE(observed.WaitForRequest(2s));

        push::v1::PushRequest request;
        ASSERT_TRUE(request.ParseFromString(observed.Body));
        ASSERT_EQ(request.series_size(), 1);

        const auto labels = GetSeriesLabels(request.series(0));
        EXPECT_EQ(labels.at("process.runtime.name"), ".NET Framework");
        EXPECT_EQ(labels.at("process.runtime.version"), "4.8.9195");
    }

    server.stop();
    serverThread.join();
}

TEST(PyroscopePprofSinkTest, UploadAddsSemanticSeriesLabels)
{
    httplib::Server server;
    ObservedRequest observed;
    const std::string expectedPath = "/push.v1.PusherService/Push";

    server.Post(expectedPath, [](const httplib::Request&, httplib::Response& res) { res.status = 200; });
    server.set_logger([&observed](const httplib::Request& req, const httplib::Response& res) {
        observed.Set(req.path, res.status, req.body);
    });

    auto port = server.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);

    std::thread serverThread([&server]() {
        server.listen_after_bind();
    });
    server.wait_until_ready();

    {
        const auto url = "http://127.0.0.1:" + std::to_string(port);
        RuntimeInfo runtimeInfo(8, 0, false);
        PyroscopePprofSink sink(
            url, "service", BasicAuth{}, PyroscopeTenantId{}, {}, &runtimeInfo, {{"static_label", "static_value"}});

        Pprof pprof;
        pprof.bytes = "fake-pprof";
        pprof.profileType = ProfileType::ProcessCpu;

        sink.Export({std::move(pprof)});

        ASSERT_TRUE(observed.WaitForRequest(2s));
        EXPECT_EQ(observed.Path, expectedPath);
        EXPECT_EQ(observed.Status, 200);

        push::v1::PushRequest request;
        ASSERT_TRUE(request.ParseFromString(observed.Body));
        ASSERT_EQ(request.series_size(), 1);

        const auto labels = GetSeriesLabels(request.series(0));
        EXPECT_EQ(labels.at("__name__"), "process_cpu");
        EXPECT_EQ(labels.at("service_name"), "service");
        EXPECT_EQ(labels.at("spy_name"), "dotnetspy");
        EXPECT_EQ(labels.at("otel.scope.name"), "com.grafana.pyroscope/dotnet");
        EXPECT_EQ(labels.at("otel.scope.version"), PROFILER_VERSION);
        EXPECT_EQ(labels.at("process.runtime.name"), ".NET");
        EXPECT_EQ(labels.at("process.runtime.version"), "8.0");
        EXPECT_EQ(labels.at("static_label"), "static_value");
    }

    server.stop();
    serverThread.join();
}

namespace
{
std::optional<ProxyConfig> ResolveProxy(const std::string& server, const std::map<shared::WSTRING, std::string>& env)
{
    return PyroscopePprofSink::ResolveProxy(Url(server), [&env](const shared::WSTRING& name) {
        auto it = env.find(name);
        return it == env.end() ? std::string{} : it->second;
    });
}
} // namespace

TEST(PyroscopePprofSinkTest, ResolveProxyReturnsNothingWithoutProxyVariables)
{
    EXPECT_FALSE(ResolveProxy("https://pyroscope.example.com", {}).has_value());
}

TEST(PyroscopePprofSinkTest, ResolveProxyUsesSchemeSpecificVariable)
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

TEST(PyroscopePprofSinkTest, ResolveProxyUsesLowercaseVariables)
{
    auto proxy = ResolveProxy("https://pyroscope.example.com", {{WStr("https_proxy"), "proxy:3128"}});
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "proxy");
    EXPECT_EQ(proxy->port, 3128);
}

TEST(PyroscopePprofSinkTest, ResolveProxyParsesCredentialsAndDefaultPort)
{
    auto proxy = ResolveProxy("https://pyroscope.example.com", {{WStr("HTTPS_PROXY"), "http://us%40er:p%3Ass@proxy"}});
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "proxy");
    EXPECT_EQ(proxy->port, 80);
    EXPECT_EQ(proxy->user, "us@er");
    EXPECT_EQ(proxy->password, "p:ss");
}

TEST(PyroscopePprofSinkTest, ResolveProxyPrefersPyroscopeProxy)
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

TEST(PyroscopePprofSinkTest, ResolveProxyHonorsNoProxy)
{
    const std::map<shared::WSTRING, std::string> env = {
        {WStr("HTTPS_PROXY"), "http://proxy:3128"},
        {WStr("no_proxy"), "localhost, .internal.example.com"},
    };

    EXPECT_FALSE(ResolveProxy("https://pyroscope.internal.example.com", env).has_value());
    EXPECT_TRUE(ResolveProxy("https://pyroscope.example.com", env).has_value());
}

TEST(PyroscopePprofSinkTest, ResolveProxyRejectsUnsupportedProxies)
{
    EXPECT_FALSE(ResolveProxy("https://pyroscope.example.com", {{WStr("HTTPS_PROXY"), "https://proxy:3128"}}).has_value());
    EXPECT_FALSE(ResolveProxy("https://pyroscope.example.com", {{WStr("HTTPS_PROXY"), "socks5://proxy:1080"}}).has_value());
    EXPECT_FALSE(ResolveProxy("https://pyroscope.example.com", {{WStr("HTTPS_PROXY"), "http://proxy:port"}}).has_value());
}

TEST(PyroscopePprofSinkTest, NoProxyMatchesDomains)
{
    EXPECT_FALSE(PyroscopePprofSink::NoProxyMatches("", "example.com"));
    EXPECT_TRUE(PyroscopePprofSink::NoProxyMatches("*", "example.com"));
    EXPECT_TRUE(PyroscopePprofSink::NoProxyMatches("example.com", "example.com"));
    EXPECT_TRUE(PyroscopePprofSink::NoProxyMatches("example.com", "api.example.com"));
    EXPECT_TRUE(PyroscopePprofSink::NoProxyMatches(".example.com", "example.com"));
    EXPECT_TRUE(PyroscopePprofSink::NoProxyMatches("*.example.com", "api.example.com"));
    EXPECT_TRUE(PyroscopePprofSink::NoProxyMatches("other.com, EXAMPLE.com", "example.com"));
    EXPECT_FALSE(PyroscopePprofSink::NoProxyMatches("example.com", "badexample.com"));
    EXPECT_FALSE(PyroscopePprofSink::NoProxyMatches("api.example.com", "example.com"));
    EXPECT_FALSE(PyroscopePprofSink::NoProxyMatches(",,.", "example.com"));
}

TEST(PyroscopePprofSinkTest, NoProxyMatchesIpAddresses)
{
    EXPECT_TRUE(PyroscopePprofSink::NoProxyMatches("127.0.0.1", "127.0.0.1"));
    EXPECT_FALSE(PyroscopePprofSink::NoProxyMatches("127.0.0.1", "127.0.0.2"));
    EXPECT_TRUE(PyroscopePprofSink::NoProxyMatches("10.0.0.0/8", "10.1.2.3"));
    EXPECT_FALSE(PyroscopePprofSink::NoProxyMatches("10.0.0.0/8", "11.1.2.3"));
    EXPECT_TRUE(PyroscopePprofSink::NoProxyMatches("192.168.1.0/23", "192.168.0.7"));
    EXPECT_FALSE(PyroscopePprofSink::NoProxyMatches("192.168.1.0/24", "192.168.0.7"));
    EXPECT_TRUE(PyroscopePprofSink::NoProxyMatches("::1", "::1"));
    EXPECT_TRUE(PyroscopePprofSink::NoProxyMatches("[fd00::]/8", "fd12::1"));
    EXPECT_FALSE(PyroscopePprofSink::NoProxyMatches("10.0.0.0/8", "::1"));
    EXPECT_FALSE(PyroscopePprofSink::NoProxyMatches("10.0.0.0/33", "10.0.0.1"));
    EXPECT_FALSE(PyroscopePprofSink::NoProxyMatches("10.0.0.0/8", "example.com"));
}

TEST(PyroscopePprofSinkTest, UploadGoesThroughProxy)
{
    httplib::Server proxy;
    ObservedRequest observed;

    proxy.Post(".*", [](const httplib::Request&, httplib::Response& res) { res.status = 200; });
    proxy.set_logger([&observed](const httplib::Request& req, const httplib::Response& res) {
        observed.Set(req.target, res.status, req.body, req.get_header_value("Proxy-Authorization"));
    });

    auto port = proxy.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);

    std::thread proxyThread([&proxy]() {
        proxy.listen_after_bind();
    });
    proxy.wait_until_ready();

    {
        EnvironmentHelper::EnvironmentVariable proxyEnv(
            EnvironmentVariables::PyroscopeProxy,
            shared::ToWSTRING("http://proxy-user:proxy-password@127.0.0.1:" + std::to_string(port)));
        RuntimeInfo runtimeInfo(8, 0, false);
        PyroscopePprofSink sink(
            "http://pyroscope.invalid:4040/tenant", "service", BasicAuth{}, PyroscopeTenantId{}, {}, &runtimeInfo, {});

        Pprof pprof;
        pprof.bytes = "fake-pprof";
        pprof.profileType = ProfileType::ProcessCpu;

        sink.Export({std::move(pprof)});

        ASSERT_TRUE(observed.WaitForRequest(2s));
        EXPECT_EQ(observed.Path, "http://pyroscope.invalid:4040/tenant/push.v1.PusherService/Push");
        EXPECT_EQ(observed.Status, 200);
        EXPECT_EQ(
            observed.Authorization,
            "Basic " + cppcodec::base64_rfc4648::encode(std::string{"proxy-user:proxy-password"}));
    }

    proxy.stop();
    proxyThread.join();
}
