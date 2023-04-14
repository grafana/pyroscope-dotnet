//
// Created by root on 11/3/22.
//

#pragma once

#include <PprofExporter.h>

#include "LockingQueue.h"
#include "httplib.h"
#include "url.hpp"

class PyroscopePprofSink: public PProfExportSink
{
public:
    PyroscopePprofSink(std::string server, std::string appName, std::string authToken, std::map<std::string, std::string> extraHeaders);
    void Export(Pprof pprof, ProfileTime &startTime, ProfileTime &endTime) override;
    ~PyroscopePprofSink() override;
    static std::map<std::string, std::string> ParseHeadersJSON(std::string headers);

private:
    static std::string SchemeHostPort(Url &url);

    struct PyroscopeRequest {
        Pprof pprof;
        ProfileTime startTime;
        ProfileTime endTime;
    };

    void work();
    void upload(Pprof pprof, ProfileTime &startTime, ProfileTime &endTime);

    std::string _appName;
    Url _url;
    httplib::Client _client;
    std::atomic<bool> _running;
    LockingQueue<PyroscopeRequest> _queue;
    std::thread _workerThread;
};


