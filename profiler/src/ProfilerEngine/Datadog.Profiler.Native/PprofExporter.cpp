//
// Created by root on 11/2/22.
//

#include "PprofExporter.h"
#include "ISamplesProvider.h"
#include "SamplesEnumerator.h"
#include "Log.h"
#include <signal.h>

PprofExporter::PprofExporter(IApplicationStore* applicationStore,
                             std::shared_ptr<PProfExportSink> sink,
                             std::vector<SampleValueType> sampleTypeDefinitions) :
    _applicationStore(applicationStore),
    _sink(std::move(sink)),
    _sampleTypeDefinitions(sampleTypeDefinitions),
    _processSampleTypeDefinitions(_sampleTypeDefinitions)
{
    for (auto& sampleType : _processSampleTypeDefinitions)
    {
        if (sampleType.Name == "cpu")
        {
            sampleType.Name = "gc_cpu";
        }
    }

    _builder = std::make_unique<PprofBuilder>(_sampleTypeDefinitions);
    _processSamplesBuilder = std::make_unique<PprofBuilder>(_processSampleTypeDefinitions);
    signal(SIGPIPE, SIG_IGN);
}

PProfExportSink::~PProfExportSink()
{
}

void PprofExporter::Add(std::shared_ptr<Sample> const& sample)
{
    std::lock_guard lock(_builderLock);
    _builder->AddSample(*sample);
}

void PprofExporter::SetEndpoint(const std::string& runtimeId, uint64_t traceId, const std::string& endpoint)
{
}

bool PprofExporter::Export(ProfileTime& startTime, ProfileTime& endTime, bool lastCall)
{
    std::vector<std::string> pprofs;
    {
        std::lock_guard lock(_builderLock);
        if (_builder->SamplesCount() != 0)
        {
            pprofs.emplace_back(_builder->Build(startTime, endTime));
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
            pprofs.emplace_back(_processSamplesBuilder->Build(startTime, endTime));
        }
    }

    for (const auto& pprof : pprofs)
    {
        _sink->Export(std::move(pprof));
    }
    return true;
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