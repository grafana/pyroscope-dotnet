//
// Created by root on 11/3/22.
//

#pragma once

#include "IRuntimeInfo.h"
#include "PprofExporter.h"

#include "pyroscope_uploader.h"

#include <string>
#include <utility>
#include <vector>

struct BasicAuth
{
    std::string user;
    std::string password;
};

struct PyroscopeTenantId
{
    std::string value;
};

class PyroscopePprofSink : public PProfExportSink
{
public:
    using Labels = std::vector<std::pair<std::string, std::string>>;

    PyroscopePprofSink(std::string server,
                       std::string appName,
                       BasicAuth basicAuth,
                       PyroscopeTenantId tenantId,
                       std::string httpHeadersJson,
                       IRuntimeInfo* runtimeInfo,
                       const Labels& staticTags);
    ~PyroscopePprofSink() override;
    PyroscopePprofSink(const PyroscopePprofSink&) = delete;
    PyroscopePprofSink& operator=(const PyroscopePprofSink&) = delete;

    void Export(std::vector<Pprof> pprofs) override;
    void SetBasicAuth(BasicAuth basicAuth);
    Labels GetLabels(ProfileType profileType) const;

private:
    std::string _appName;
    IRuntimeInfo* _runtimeInfo;
    Labels _staticTags;
    PyroscopeUploader* _uploader;
};
