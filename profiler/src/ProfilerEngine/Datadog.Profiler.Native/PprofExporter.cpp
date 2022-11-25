//
// Created by root on 11/2/22.
//

#include "PprofExporter.h"
#include "Log.h"

PprofExporter::PprofExporter(IApplicationStore* applicationStore,
                             std::unique_ptr<PProfExportSink> sink,
                             std::vector<SampleValueType> sampleTypeDefinitions,
                             const std::vector<std::pair<std::string, std::string>>& staticTags) :
    _applicationStore(applicationStore),
    _sink(std::move(sink)),
    _sampleTypeDefinitions(sampleTypeDefinitions),
    _staticTags(staticTags)
{
    signal(SIGPIPE, SIG_IGN);
}

PProfExportSink::~PProfExportSink()
{
}

void PprofExporter::Add(const Sample& sample)
{
    GetPprofBuilder(sample.GetRuntimeId())
        .AddSample(sample);
}

void PprofExporter::SetEndpoint(const std::string& runtimeId, uint64_t traceId, const std::string& endpoint)
{
}

bool PprofExporter::Export(ProfileTime& startTime, ProfileTime& endTime)
{
    std::vector<std::string> pprofs;
    {
        std::lock_guard lock(_perAppBuilderLock);
        for (auto& builder : _perAppBuilder)
        {
            pprofs.emplace_back(builder.second->Build());
        }
    }
    for (const auto& pprof : pprofs)
    {
        _sink->Export(std::move(pprof), startTime, endTime);
    }
    return true;
}

PprofBuilder& PprofExporter::GetPprofBuilder(std::string_view runtimeId)
{
    std::lock_guard lock(_perAppBuilderLock);
    auto it = _perAppBuilder.find(runtimeId);
    if (it != _perAppBuilder.end())
    {
        return *it->second;
    }
    auto instance = std::make_unique<PprofBuilder>(_sampleTypeDefinitions, _staticTags);
    auto res = _perAppBuilder.emplace(runtimeId, std::move(instance));
    return *res.first->second;
}
