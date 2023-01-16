//
// Created by root on 11/3/22.
//

#pragma once

#include <PprofExporter.h>

#include "httplib.h"
#include "LockingQueue.h"

class PyroscopePprofSink: public PProfExportSink
{
public:
    PyroscopePprofSink(std::string server, std::string appName, std::string authToken);
    virtual void Export(Pprof pprof, ProfileTime &startTime, ProfileTime &endTime);
    virtual ~PyroscopePprofSink() = default;

private:
    struct PyroscopeRequest {
        Pprof pprof;
        ProfileTime startTime;
        ProfileTime endTime;
    };

    [[noreturn]] void work();
    void upload(Pprof pprof, ProfileTime &startTime, ProfileTime &endTime);

    std::string _appName;
    std::string _server;
    httplib::Client _client;
    LockingQueue<PyroscopeRequest> _queue;
    std::thread _workerThread;
};


