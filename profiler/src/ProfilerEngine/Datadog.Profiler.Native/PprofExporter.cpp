//
// Created by root on 11/2/22.
//

#include "PprofExporter.h"
#include "Log.h"
#include <signal.h>

PprofExporter::PprofExporter(IApplicationStore* applicationStore,
                             std::shared_ptr<PProfExportSink> sink,
                             std::vector<SampleValueType> sampleTypeDefinitions) :
    _applicationStore(applicationStore),
    _sink(std::move(sink)),
    _sampleTypeDefinitions(sampleTypeDefinitions)
{
    std::vector<SampleValueType> processSampleTypeDefinitions = _sampleTypeDefinitions;
    for (auto& sampleType : processSampleTypeDefinitions)
    {
        if (sampleType.Name == "cpu")
        {
            sampleType.Name = "gc_cpu";
        }
    }

    _processSamplesBuilder = std::make_unique<PprofBuilder>(processSampleTypeDefinitions);
    signal(SIGPIPE, SIG_IGN);
}

PProfExportSink::~PProfExportSink()
{
}

void PprofExporter::Add(std::shared_ptr<Sample> const& sample)
{
    GetPprofBuilder(sample->GetRuntimeId())
        .AddSample(*sample);
}

void PprofExporter::SetEndpoint(const std::string& runtimeId, uint64_t traceId, const std::string& endpoint)
{
}

bool PprofExporter::Export(ProfileTime& startTime, ProfileTime& endTime, bool lastCall)
{
    std::vector<std::string> pprofs;
    {
        std::lock_guard lock(_perAppBuilderLock);
        for (auto& builder : _perAppBuilder)
        {
            if (builder.second->SamplesCount() != 0)
            {
                pprofs.emplace_back(builder.second->Build());
            }
        }
    }

    {
        std::lock_guard lock(_processSamplesLock);
        for (auto provider : _processSamplesProviders)
        {
            auto samplesEnumerator = provider->GetSamples();
            std::shared_ptr<Sample> sample;
            while (samplesEnumerator->MoveNext(sample))
            {
                _processSamplesBuilder->AddSample(*sample);
            }
        }

        if (_processSamplesBuilder->SamplesCount() > 0)
        {
            pprofs.emplace_back(_processSamplesBuilder->Build());
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
    auto instance = std::make_unique<PprofBuilder>(_sampleTypeDefinitions);
    auto res = _perAppBuilder.emplace(runtimeId, std::move(instance));
    return *res.first->second;
}

void PprofExporter::RegisterUpscaleProvider(IUpscaleProvider* provider) {};
void PprofExporter::RegisterProcessSamplesProvider(ISamplesProvider* provider)
{
    std::lock_guard lock(_processSamplesLock);
    _processSamplesProviders.push_back(provider);
};
void PprofExporter::RegisterApplication(std::string_view runtimeId) {};