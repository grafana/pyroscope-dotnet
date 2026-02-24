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
#include <memory>
#include <span>
#include <vector>

using Pprof = std::string;

class PProfExportSink
{
public:
    virtual void Export(std::vector<Pprof> pprofs, ProfileTime& startTime, ProfileTime& endTime) = 0;
    virtual ~PProfExportSink();
};

struct ProfileTypeEntry
{
    ProfileTypeEntry(size_t startIndex, size_t count, std::vector<SampleValueType> sampleTypes) :
        startIndex(startIndex), count(count), builder(std::move(sampleTypes)) {}

    size_t startIndex;                 // offset into Sample::GetValues()
    size_t count;                      // number of values for this profile type
    PprofBuilder builder;
    std::mutex lock;
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
    void AddSampleToEntries(const Sample& sample);

    IApplicationStore* _applicationStore;
    std::shared_ptr<PProfExportSink> _sink;
    std::vector<std::unique_ptr<ProfileTypeEntry>> _entries;

    std::vector<ISamplesProvider*> _processSamplesProviders;
};
