//
// Created by root on 11/3/22.
//

#include "PyroscopePprofSink.h"
#include "Log.h"
#include "OpSysTools.h"

PyroscopePprofSink::PyroscopePprofSink(std::string server, std::string appName, std::string authToken) :
    _appName(appName),
    _url(server),
    _client(SchemeHostPort(_url)),
    _running(true)
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

PyroscopePprofSink::~PyroscopePprofSink()
{
    _running.store(false);
    _queue.push(PyroscopeRequest{
        .pprof = "",
        .startTime = ProfileTime(),
        .endTime = ProfileTime(),
    });
    _workerThread.join();
}

void PyroscopePprofSink::Export(Pprof pprof, ProfileTime& startTime, ProfileTime& endTime)
{
    if (_queue.size() >= 3)
    {
        Log::Info("PyroscopePprofSink queue is too big. Dropping pprof data.");
        return;
    }
    PyroscopeRequest req{
        .pprof = std::move(pprof),
        .startTime = startTime,
        .endTime = endTime,
    };
    _queue.push(req);
}

void PyroscopePprofSink::work()
{
    while (_running.load())
    {
        PyroscopeRequest req = {};
        _queue.waitAndPop(req);
        if (req.pprof.empty())
        {
            continue;
        }
        upload(std::move(req.pprof), req.startTime, req.endTime);
    }
}

void PyroscopePprofSink::upload(Pprof pprof, ProfileTime& startTime, ProfileTime& endTime)
{
    httplib::MultipartFormDataItems data;
    data.emplace_back(httplib::MultipartFormData{
        .name = "profile",
        .content = std::move(pprof),
        .filename = "profile.pprof",
    });
    data.emplace_back(httplib::MultipartFormData{
        .name = "sample_type_config",
        .content = "{\n"
                   "  \"alloc-samples\": {\n"
                   "    \"units\": \"objects\",\n"
                   "    \"display-name\": \"alloc_objects\"\n"
                   "  },\n"
                   "  \"alloc-size\": {\n"
                   "    \"units\": \"bytes\",\n"
                   "    \"display-name\": \"alloc_space\"\n"
                   "  },\n"
                   "  \"cpu\": {\n"
                   "    \"units\": \"samples\",\n"
                   "    \"sampled\": true\n"
                   "  },\n"
                   "  \"exception\": {\n"
                   "    \"units\": \"exceptions\",\n"
                   "    \"display-name\": \"exceptions\"\n"
                   "  },\n"
                   "  \"lock-count\": {\n"
                   "    \"units\": \"lock_samples\",\n"
                   "    \"display-name\": \"mutex_count\"\n"
                   "  },\n"
                   "  \"lock-time\": {\n"
                   "    \"units\": \"lock_nanoseconds\",\n"
                   "    \"display-name\": \"mutex_duration\"\n"
                   "  },\n"
                   "  \"wall\": {\n"
                   "    \"units\": \"samples\",\n"
                   "    \"sampled\": true\n"
                   "  },\n"
                   "  \"inuse-objects\": {\n"
                   "    \"units\": \"objects\",\n"
                   "    \"display-name\": \"inuse_objects\",\n"
                   "    \"aggregation\": \"average\"\n"
                   "  },\n"
                   "  \"inuse-space\": {\n"
                   "    \"units\": \"bytes\",\n"
                   "    \"display-name\": \"inuse_space\",\n"
                   "    \"aggregation\": \"average\"\n"
                   "  }\n"
                   "}",
        .filename = "sample_type_config.json",
    });

    std::string path = Url()
                           .path(_url.path() + "/ingest")
                           .add_query("name", _appName)
                           .add_query("from", std::to_string(startTime.time_since_epoch().count()))
                           .add_query("until", std::to_string(endTime.time_since_epoch().count()))
                           .str();

    auto res = _client.Post(path, data);
    if (res)
    {
        Log::Info("PyroscopePprofSink ", res->status);
    }
    else
    {
        auto err = res.error();
        Log::Info("PyroscopePprofSink err ", to_string(err), " ", _url);
    }
}

std::string PyroscopePprofSink::SchemeHostPort(Url& url)
{
    if (url.scheme().empty())
    {
        throw std::runtime_error("empty scheme " + url.str());
    }
    if (url.host().empty())
    {
        throw std::runtime_error("empty host " + url.str());
    }
    if (url.port().empty())
    {
        throw std::runtime_error("empty port " + url.str());
    }
    return url.scheme() + "://" + url.host() + ":" + url.port();
}
