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
#include <unordered_map>
#include <vector>


using Pprof = std::string;

class PProfExportSink
{
public:
    virtual void Export(Pprof pprof, ProfileTime &startTime, ProfileTime &endTime) = 0;
    virtual ~PProfExportSink();
};

class PprofExporter : public IExporter
{

public:
    PprofExporter(IApplicationStore* _applicationStore,
                  std::shared_ptr<PProfExportSink> sin,
                  std::vector<SampleValueType> sampleTypeDefinitions
    );
    void Add(std::shared_ptr<Sample> const& sample) override;
    void SetEndpoint(const std::string& runtimeId, uint64_t traceId, const std::string& endpoint) override;
    bool Export(ProfileTime& startTime, ProfileTime& endTime, bool lastCall = false) override;
    void RegisterUpscaleProvider(IUpscaleProvider* provider) override;
    void RegisterProcessSamplesProvider(ISamplesProvider* provider) override;
    void RegisterApplication(std::string_view runtimeId) override;
private:
    PprofBuilder& GetPprofBuilder(std::string_view runtimeId);

    IApplicationStore* _applicationStore;
    std::shared_ptr<PProfExportSink> _sink;
    std::vector<SampleValueType> _sampleTypeDefinitions;
    std::unordered_map<std::string_view, std::unique_ptr<PprofBuilder>> _perAppBuilder;
    std::mutex _perAppBuilderLock;
};
