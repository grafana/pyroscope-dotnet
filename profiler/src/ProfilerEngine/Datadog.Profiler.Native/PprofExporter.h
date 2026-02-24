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

#include <forward_list>
#include <map>
#include <memory>
#include <span>
#include <vector>

using Pprof = std::string;

class PProfExportSink
{
public:
    virtual void Export(Pprof pprof, ProfileTime& startTime, ProfileTime& endTime) = 0;
    virtual ~PProfExportSink();
};

struct ProfileTypeEntry
{
    std::vector<SampleValueType> sampleTypes; // subset of types for this profile type group
    size_t startIndex;                         // offset into Sample::GetValues()
    size_t count;                              // == sampleTypes.size()
    std::unique_ptr<PprofBuilder> builder;     // holds reference to sampleTypes — must not move after init
};

class PprofExporter : public IExporter
{

public:
    PprofExporter(IApplicationStore* _applicationStore,
                  std::shared_ptr<PProfExportSink> sin,
                  std::vector<SampleValueType> sampleTypeDefinitions);
    void Add(std::shared_ptr<Sample> const& sample) override;
    void SetEndpoint(const std::string& runtimeId, uint64_t traceId, const std::string& endpoint) override;
    bool Export(ProfileTime& startTime, ProfileTime& endTime, bool lastCall = false) override;
    void RegisterUpscaleProvider(IUpscaleProvider* provider) override;
    void RegisterUpscalePoissonProvider(IUpscalePoissonProvider* provider) override;
    void RegisterProcessSamplesProvider(ISamplesProvider* provider) override;
    void RegisterApplication(std::string_view runtimeId) override;
    void RegisterGcSettingsProvider(IGcSettingsProvider* provider) override;

private:
    static bool AllZero(std::span<const int64_t> values);

    IApplicationStore* _applicationStore;
    std::shared_ptr<PProfExportSink> _sink;
    std::vector<ProfileTypeEntry> _entries;
    std::mutex _builderLock;

    std::vector<ISamplesProvider*> _processSamplesProviders;
};
