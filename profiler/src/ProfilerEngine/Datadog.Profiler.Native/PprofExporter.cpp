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
    _sink(std::move(sink))
{
    // Sample types are always laid out in contiguous blocks of the same ProfileType.
    // Scan linearly: start a new entry whenever the ProfileType changes.
    size_t i = 0;
    while (i < sampleTypeDefinitions.size())
    {
        ProfileType currentType = sampleTypeDefinitions[i].Type;
        size_t startIndex = i;

        std::vector<SampleValueType> groupTypes;
        while (i < sampleTypeDefinitions.size() && sampleTypeDefinitions[i].Type == currentType)
        {
            groupTypes.push_back(sampleTypeDefinitions[i]);
            ++i;
        }

        _entries.push_back(std::make_unique<ProfileTypeEntry>(startIndex, groupTypes.size(), currentType, std::move(groupTypes)));
    }

    signal(SIGPIPE, SIG_IGN);
}

PProfExportSink::~PProfExportSink()
{
}

// static
bool PprofExporter::AllZero(std::span<const int64_t> values)
{
    for (auto v : values)
        if (v != 0) return false;
    return true;
}

void PprofExporter::AddSampleToEntries(const Sample& sample)
{
    auto const& allValues = sample.GetValues();
    for (auto& entry : _entries)
    {
        auto slice = std::span<const int64_t>(allValues.data() + entry->startIndex, entry->count);
        if (AllZero(slice)) continue;
        entry->builder.AddSample(sample, slice);
    }
}

void PprofExporter::Add(std::shared_ptr<Sample> const& sample)
{
    AddSampleToEntries(*sample);
}

void PprofExporter::SetEndpoint(const std::string& runtimeId, uint64_t traceId, const std::string& endpoint)
{
}

bool PprofExporter::Export(ProfileTime& startTime, ProfileTime& endTime, bool lastCall)
{
    // Collect process samples (e.g. GcThreadsCpu) through the same per-entry dispatch.
    {
        std::lock_guard lock(_processSamplesLock);
        for (auto provider : _processSamplesProviders)
        {
            auto samplesEnumerator = provider->GetSamples();
            std::shared_ptr<Sample> sample;
            while (samplesEnumerator->MoveNext(sample))
            {
                AddSampleToEntries(*sample);
            }
        }
    }

    std::vector<Pprof> pprofs;
    for (auto& entry : _entries)
    {
        auto bytes = entry->builder.Build(startTime, endTime);
        if (!bytes.empty())
            pprofs.push_back({std::move(bytes), entry->profileType});
    }

    if (!pprofs.empty())
    {
        _sink->Export(std::move(pprofs));
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
