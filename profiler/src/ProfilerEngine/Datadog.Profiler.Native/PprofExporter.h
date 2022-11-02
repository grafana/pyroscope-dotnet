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

class PProfExportSink
{
public:
    virtual void Export(std::string pprof) = 0;
    virtual ~PProfExportSink();
};

class PprofExporter : public IExporter
{

public:
    PprofExporter(IApplicationStore* _applicationStore, std::unique_ptr<PProfExportSink> sin, std::vector<SampleValueType> sampleTypeDefinitions);
    void Add(const Sample& sample) override;
    void SetEndpoint(const std::string& runtimeId, uint64_t traceId, const std::string& endpoint) override;
    bool Export() override;

private:
    PprofBuilder& GetPprofBuilder(std::string_view runtimeId);

    IApplicationStore* _applicationStore;
    std::unique_ptr<PProfExportSink> _sink;
    std::vector<SampleValueType> _sampleTypeDefinitions;
    std::unordered_map<std::string_view, PprofBuilder> _perAppBuilder;
    std::mutex _perAppBuilderLock;
};
