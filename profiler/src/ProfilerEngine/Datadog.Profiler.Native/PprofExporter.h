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
#include "profile.pb.h"

#include <forward_list>
#include <memory>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

using Pprof = std::string;

class PProfExportSink
{
public:
    virtual void Export(Pprof pprof, ProfileType type, const ProfileTime& startTime, const ProfileTime& endTime) = 0;
    virtual ~PProfExportSink();
};

class PprofExporter : public IExporter
{

public:
    PprofExporter(std::shared_ptr<PProfExportSink> sin,
                  std::vector<SampleValueType> sampleTypeDefinitions);
    void Add(std::shared_ptr<Sample> const& sample) override;
    void SetEndpoint(const std::string& runtimeId, uint64_t traceId, const std::string& endpoint) override;
    bool Export(ProfileTime& startTime, ProfileTime& endTime, bool lastCall = false) override;
    void RegisterUpscaleProvider(IUpscaleProvider* provider) override;
    void RegisterUpscalePoissonProvider(IUpscalePoissonProvider* provider) override;
    void RegisterProcessSamplesProvider(ISamplesProvider* provider) override;
    void RegisterApplication(std::string_view runtimeId) override;

private:
    ProfileType GetPprofType(std::shared_ptr<Sample> const& sample, bool &found);
    PprofBuilder& GetPprofBuilder(ProfileType type);

    std::shared_ptr<PProfExportSink> _sink;
    std::vector<SampleValueType> _sampleTypeDefinitions;
    std::vector<SampleValueType> _processSampleTypeDefinitions;
    std::unordered_map<ProfileType, std::unique_ptr<PprofBuilder>> _perProfileTypeBuilder;
    std::mutex _perProfileTypeBuilderLock;

    std::vector<ISamplesProvider*> _processSamplesProviders;
    std::unique_ptr<PprofBuilder> _processSamplesBuilder;
    std::mutex _processSamplesLock;
};
