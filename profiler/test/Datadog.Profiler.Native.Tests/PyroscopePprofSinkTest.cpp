// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "gtest/gtest.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "PyroscopePprofSink.h"

using namespace std::chrono_literals;

namespace
{
struct ObservedRequest
{
    void Set(std::string requestPath, int responseStatus)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (Seen)
        {
            return;
        }

        Path = std::move(requestPath);
        Status = responseStatus;
        Seen = true;
        Cv.notify_one();
    }

    bool WaitForRequest(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(Mutex);
        return Cv.wait_for(lock, timeout, [this] { return Seen; });
    }

    std::string Path;
    int Status = -1;

private:
    std::mutex Mutex;
    std::condition_variable Cv;
    bool Seen = false;
};

void ValidatePushPath(const std::string& serverPath, const std::string& expectedPath)
{
    httplib::Server server;
    ObservedRequest observed;

    server.Post(expectedPath, [](const httplib::Request&, httplib::Response& res) { res.status = 200; });
    server.set_logger([&observed](const httplib::Request& req, const httplib::Response& res) {
        observed.Set(req.path, res.status);
    });

    auto port = server.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);

    std::thread serverThread([&server]() {
        server.listen_after_bind();
    });
    server.wait_until_ready();

    {
        const auto url = "http://127.0.0.1:" + std::to_string(port) + serverPath;
        PyroscopePprofSink sink(url, "service", "", "", "", "", {}, {});

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
