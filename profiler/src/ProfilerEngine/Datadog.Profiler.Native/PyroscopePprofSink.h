//
// Created by root on 11/3/22.
//

#pragma once

#include "PprofExporter.h"

#include "LockingQueue.h"
// A remote server might not honor "Expect: 100-continue". cpp-httplib adds that
// header automatically once a request body reaches its default 1 KiB threshold,
// then withholds the body waiting for a "100 Continue" that never arrives, so
// every profile upload larger than ~1 KiB fails. Disable the behavior (0 = never)
// so the body is always sent with the headers.
#define CPPHTTPLIB_EXPECT_100_THRESHOLD 0
#ifdef _MSC_VER
// cpp-httplib's OpenSSL path trips C4996 (sscanf, and the library calling its own
// [[deprecated]] TLS helpers), which /sdl elevates to errors. Suppress for this
// third-party header only; our own code keeps the /sdl checks.
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#include "httplib.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include "url.hpp"

#define PYROSCOPE_SPY_VERSION "1.3.0" // x-release-please-version

class PyroscopePprofSink : public PProfExportSink
{
public:
    PyroscopePprofSink(std::string server,
                       std::string appName,
                       std::string authToken,
                       std::string tenantID,
                       std::string basicAuthUser,
                       std::string basicAuthPassword,
                       std::map<std::string, std::string> extraHeaders,
                       const std::vector<std::pair<std::string, std::string>>& staticTags);
    ~PyroscopePprofSink() override;
    void Export(std::vector<Pprof> pprofs) override;
    void SetAuthToken(std::string authToken);
    void SetBasicAuth(std::string user, std::string password);
    static std::map<std::string, std::string> ParseHeadersJSON(std::string headers);

private:
    static std::string SchemeHostPort(Url& url);

    struct PyroscopeRequest
    {
        std::vector<Pprof> pprofs;
    };

    void work();
    void upload(Pprof pprof);
    httplib::Headers getHeaders();

    std::string _appName;
    std::vector<std::pair<std::string, std::string>> _staticTags;
    Url _url;
    httplib::Client _client;
    std::atomic<bool> _running;
    LockingQueue<PyroscopeRequest> _queue;
    std::thread _workerThread;

    std::string _authToken;
    std::string _basicAuthUser;
    std::string _basicAuthPassword;
    std::string _tenantID;
    std::map<std::string, std::string> _extraHeaders;
    std::mutex _authLock;
};
