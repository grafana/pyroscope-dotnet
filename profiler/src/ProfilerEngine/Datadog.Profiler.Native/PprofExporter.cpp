//
// Created by root on 11/2/22.
//

#include "PprofExporter.h"
#include "ISamplesProvider.h"
#include "SamplesEnumerator.h"
#include "Log.h"
#include <signal.h>

PprofExporter::PprofExporter(IApplicationStore* applicationStore,
                             std::shared_ptr<PProfExportSink> sink) :
    _applicationStore(applicationStore),
    _sink(std::move(sink))
{
    signal(SIGPIPE, SIG_IGN);
}

PProfExportSink::~PProfExportSink()
{
}

void PprofExporter::Add(std::shared_ptr<Sample> const& sample)
{
    std::lock_guard lock(_appBuildersLock);
    AddSample(sample, _appBuilders);
}

void PprofExporter::SetEndpoint(const std::string& runtimeId, uint64_t traceId, const std::string& endpoint)
{
}

bool PprofExporter::Export(ProfileTime& startTime, ProfileTime& endTime, bool lastCall)
{
    {
        std::lock_guard lock(_processSamplesLock);
        for (auto provider : _processSamplesProviders)
        {
            auto samplesEnumerator = provider->GetSamples();
            std::shared_ptr<Sample> sample;
            while (samplesEnumerator->MoveNext(sample))
            {
                AddSample(sample, _processBuilders);
            }
        }
    }

    std::vector<std::string> pprofs;
    {
        std::lock_guard lock(_appBuildersLock);
        for (auto& builder : _appBuilders)
        {
            if (builder.second->SamplesCount() != 0)
            {
                pprofs.emplace_back(builder.second->Build());
            }
        }
    }

    {
        std::lock_guard lock(_processSamplesLock);
        for (auto& builder : _processBuilders)
        {
            if (builder.second->SamplesCount() != 0)
            {
                pprofs.emplace_back(builder.second->Build());
            }
        }
    }

    for (const auto& pprof : pprofs)
    {
        _sink->Export(std::move(pprof), startTime, endTime);
    }
    return true;
}

void PprofExporter::AddSample(std::shared_ptr<Sample> const& sample, std::map<SampleProfileType, std::unique_ptr<PprofBuilder>>& builders)
{
    auto sampleTypes = sample->GetSampleTypes();
    if (sampleTypes == nullptr)
    {
        return;
    }

    auto profileType = sample->GetProfileType();
    if (profileType == SampleProfileType::Unknown)
    {
        return;
    }

    GetPprofBuilder(profileType, *sampleTypes, builders).AddSample(*sample);
}

PprofBuilder& PprofExporter::GetPprofBuilder(SampleProfileType profileType,
                                             std::vector<SampleValueType> const& sampleTypes,
                                             std::map<SampleProfileType, std::unique_ptr<PprofBuilder>>& builders)
{
    auto it = builders.find(profileType);
    if (it != builders.end())
    {
        return *it->second;
    }

    auto instance = std::make_unique<PprofBuilder>(sampleTypes);
    auto result = builders.emplace(profileType, std::move(instance));
    return *result.first->second;
}

void PprofExporter::RegisterUpscaleProvider(IUpscaleProvider* provider) {};
void PprofExporter::RegisterUpscalePoissonProvider(IUpscalePoissonProvider* provider) {};

void PprofExporter::RegisterProcessSamplesProvider(ISamplesProvider* provider)
{
    std::lock_guard lock(_processSamplesLock);
    _processSamplesProviders.push_back(provider);
};
void PprofExporter::RegisterApplication(std::string_view runtimeId) {};
void PprofExporter::RegisterGcSettingsProvider(IGcSettingsProvider* provider) {};
