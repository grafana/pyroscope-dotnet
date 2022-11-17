//
// Created by root on 11/3/22.
//

#include "PyroscopePprofSink.h"
#include "Log.h"
#include "OpSysTools.h"

PyroscopePprofSink::PyroscopePprofSink(std::string server, std::string appName, std::string authToken) :
    _appName(appName), _client(server)
{
    if (!authToken.empty())
    {
        _client.set_default_headers({{"Authorization", "Bearer " + authToken}});
    }
    _client.set_connection_timeout(10);
    _client.set_read_timeout(10);

    _workerThread = std::thread(&PyroscopePprofSink::work, this);
    OpSysTools::SetNativeThreadName(&_workerThread, WStr("Pyroscope.Pprof.Uploader"));
}


void PyroscopePprofSink::Export(Pprof pprof, ProfileTime& startTime, ProfileTime& endTime)
{
    if (_queue.size() >= 3) {
        Log::Info("PyroscopePprofSink queue is too big. Dropping pprof data.");
        return;
    }
    PyroscopeRequest req{
        .pprof = std::move(pprof),
        .startTime = startTime,
        .endTime = endTime,
    };
    _queue.push(std::move(req));
}

[[noreturn]] void PyroscopePprofSink::work()
{
    while (true)
    {
        PyroscopeRequest req = {};
        _queue.waitAndPop(req);

//        FILE* f = fopen("last.pprof", "wb");
//        if (f)
//        {
//            fwrite(req.pprof.data(), req.pprof.size(), 1, f);
//            fclose(f);
//        }

        upload(std::move(req.pprof), req.startTime, req.endTime);
    }
}

void PyroscopePprofSink::upload(Pprof pprof, ProfileTime& startTime, ProfileTime& endTime)
{
    httplib::MultipartFormDataItems data;
    data.emplace_back(httplib::MultipartFormData{
        .name     = "profile",
        .content  = std::move(pprof),
        .filename = "profile.pprof",
    });
    data.emplace_back(httplib::MultipartFormData{
        .name     = "sample_type_config",
        .content  = "{\"alloc-samples\":{\"units\":\"objects\",\"display-name\":\"alloc_objects\"},\"alloc-size\":{\"units\":\"bytes\",\"display-name\":\"alloc_space\"},\"cpu\":{\"units\":\"samples\",\"sampled\":true},\"exception\":{\"units\":\"exceptions\",\"display-name\":\"exceptions\"},\"lock-count\":{\"units\":\"lock_samples\",\"display-name\":\"mutex_count\"},\"lock-time\":{\"units\":\"lock_nanoseconds\",\"display-name\":\"mutex_duration\"},\"wall\":{\"units\":\"samples\",\"sampled\":true}}",
        .filename = "sample_type_config.json",
    });


    auto res = _client.Post("/ingest?name=" + _appName +
                                "&from=" + std::to_string(startTime.time_since_epoch().count()) +
                                "&until=" + std::to_string(endTime.time_since_epoch().count()),
                            data);
    if (res)
    {
        Log::Info("PyroscopePprofSink ", res->status);
    }
    else
    {
        auto err = res.error();
        Log::Info("PyroscopePprofSink err ", to_string(err));
    }
}
