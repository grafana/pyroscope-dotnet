//
// Created by root on 11/2/22.
//

#pragma once

#include "IConfiguration.h"
#include "IExporter.h"
#include "Sample.h"
#include "TagsHelper.h"
#include <mutex>

#include "IApplicationStore.h"
#include "PprofBuilder.h"
#include "google/v1/profile.pb.h"

#include <map>
#include <memory>
#include <string_view>
#include <vector>

using Pprof = std::string;

class PProfExportSink
{
public:
    virtual void Export(Pprof pprof, ProfileTime& startTime, ProfileTime& endTime) = 0;
    virtual ~PProfExportSink();
};

class PprofExporter : public IExporter
{
public:
    PprofExporter(IApplicationStore* _applicationStore,
                  std::shared_ptr<PProfExportSink> sink);
    void Add(std::shared_ptr<Sample> const& sample) override;
    void SetEndpoint(const std::string& runtimeId, uint64_t traceId, const std::string& endpoint) override;
    bool Export(ProfileTime& startTime, ProfileTime& endTime, bool lastCall = false) override;
    void RegisterUpscaleProvider(IUpscaleProvider* provider) override;
    void RegisterUpscalePoissonProvider(IUpscalePoissonProvider* provider) override;
    void RegisterProcessSamplesProvider(ISamplesProvider* provider) override;
    void RegisterApplication(std::string_view runtimeId) override;
    void RegisterGcSettingsProvider(IGcSettingsProvider* provider) override;

private:
    void AddSample(std::shared_ptr<Sample> const& sample, std::map<SampleProfileType, std::unique_ptr<PprofBuilder>>& builders);
    PprofBuilder& GetPprofBuilder(SampleProfileType profileType,
                                  std::vector<SampleValueType> const& sampleTypes,
                                  std::map<SampleProfileType, std::unique_ptr<PprofBuilder>>& builders);

    IApplicationStore* _applicationStore;
    std::shared_ptr<PProfExportSink> _sink;

    std::map<SampleProfileType, std::unique_ptr<PprofBuilder>> _appBuilders;
    std::mutex _appBuildersLock;

    std::vector<ISamplesProvider*> _processSamplesProviders;
    std::map<SampleProfileType, std::unique_ptr<PprofBuilder>> _processBuilders;
    std::mutex _processSamplesLock;
};
