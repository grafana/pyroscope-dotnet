//
// Created by root on 11/3/22.
//

#include "PyroscopePprofSink.h"
#include "Log.h"
#include "httplib.h"

void PyroscopePprofSink::Export(std::string pprof)
{
    // todo gzip, todo keepalive + timeouts
    //todo timestamps
    httplib::Client cli("https://pyroscope.cloud");
    cli.set_default_headers({{"Authorization", "Bearer psx-iumwzbXczvJi8LbpZ18vZXBjLf1z1ui9hs8l3lUI3XLpT8Rci5Cqp3k"}});
    auto res = cli.Post("/ingest?name=tolyan.dotnet&format=pprof", pprof, "binary/octet-stream");
    if (res) {
        Log::Info("PyroscopePprofSink ", res->status);
    } else {
        auto err = res.error();
        Log::Info("PyroscopePprofSink err ", to_string(err));
    }
    FILE *f = fopen("last.pprof", "wb");
    fwrite(pprof.data(), pprof.size(), 1, f);
    fclose(f);
}