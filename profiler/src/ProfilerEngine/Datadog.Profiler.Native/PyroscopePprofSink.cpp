//
// Created by root on 11/3/22.
//

#include "PyroscopePprofSink.h"
#include "Log.h"

PyroscopePprofSink::PyroscopePprofSink(std::string server, std::string appName, std::string authToken) :
    _appName(appName), _client(server)
{
    if (!authToken.empty())
    {
        _client.set_default_headers({{"Authorization", "Bearer " + authToken}});
    }
    _client.set_connection_timeout(10);
    _client.set_read_timeout(10);
}

void PyroscopePprofSink::Export(std::string pprof, ProfileTime& startTime, ProfileTime& endTime)
{
    auto res = _client.Post("/ingest?name=" + _appName +
                                "&format=pprof" +
                                "&from=" + std::to_string(startTime.time_since_epoch().count()) +
                                "&until=" + std::to_string(endTime.time_since_epoch().count()),
                            pprof,
                            "binary/octet-stream");
    if (res)
    {
        Log::Info("PyroscopePprofSink ", res->status);
    }
    else
    {
        auto err = res.error();
        Log::Info("PyroscopePprofSink err ", to_string(err));
    }
    //    FILE* f = fopen("last.pprof", "wb");
    //    fwrite(pprof.data(), pprof.size(), 1, f);
    //    fclose(f);
}
