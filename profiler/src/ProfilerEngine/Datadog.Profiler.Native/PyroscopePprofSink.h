//
// Created by root on 11/3/22.
//

#pragma once

#include <PprofExporter.h>

#include "httplib.h"

class PyroscopePprofSink: public PProfExportSink
{
public:
    PyroscopePprofSink(std::string server, std::string appName, std::string authToken);
    virtual void Export(std::string pprof, ProfileTime &startTime, ProfileTime &endTime);
    virtual ~PyroscopePprofSink() = default;

private:
    std::string _appName;
//    std::string _authToken;
    httplib::Client _client;
};

