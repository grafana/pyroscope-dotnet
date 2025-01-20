//
// Created by root on 11/3/22.
//

#pragma once

#include <PprofExporter.h>

#include "LockingQueue.h"
#include "httplib.h"
#include "url.hpp"

#define PYROSCOPE_SPY_VERSION "0.9.4"

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
    void Export(Pprof pprof, ProfileTime& startTime, ProfileTime& endTime) override;
    void SetAuthToken(std::string authToken);
    void SetBasicAuth(std::string user, std::string password);
    static std::map<std::string, std::string> ParseHeadersJSON(std::string headers);

private:
    static std::string SchemeHostPort(Url& url);
    static std::string AppNameWithLabels(const std::string& appName, const std::vector<std::pair<std::string, std::string>>& staticTags);

    struct PyroscopeRequest
    {
        Pprof pprof;
        ProfileTime startTime;
        ProfileTime endTime;
    };

    void work();
    void upload(Pprof pprof, ProfileTime& startTime, ProfileTime& endTime);
    httplib::Headers getHeaders();

    std::string _appName;
    std::string _appNameWithLabels;
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
