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
    // Group sampleTypeDefinitions by ProfileType, preserving first-occurrence order.
    struct GroupAcc { std::vector<SampleValueType> types; size_t startIndex; };
    std::map<ProfileType, GroupAcc> groups;
    std::vector<ProfileType> order;

    for (size_t i = 0; i < sampleTypeDefinitions.size(); ++i)
    {
        const auto& svt = sampleTypeDefinitions[i];
        if (groups.find(svt.Type) == groups.end())
        {
            order.push_back(svt.Type);
            groups[svt.Type] = GroupAcc{{}, i};
        }
        groups[svt.Type].types.push_back(svt);
    }

    // Reserve before push_back: PprofBuilder holds a reference to ProfileTypeEntry::sampleTypes,
    // so _entries must never reallocate after builders are created.
    _entries.reserve(order.size());
    for (auto pt : order)
    {
        auto& acc = groups[pt];
        ProfileTypeEntry entry;
        entry.startIndex  = acc.startIndex;
        entry.count       = acc.types.size();
        entry.sampleTypes = std::move(acc.types);
        entry.builder     = std::make_unique<PprofBuilder>(entry.sampleTypes);
        _entries.push_back(std::move(entry));
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

void PprofExporter::Add(std::shared_ptr<Sample> const& sample)
{
    auto const& allValues = sample->GetValues();
    std::lock_guard lock(_builderLock);

    for (auto& entry : _entries)
    {
        auto slice = std::span<const int64_t>(allValues.data() + entry.startIndex, entry.count);
        if (AllZero(slice)) continue;
        entry.builder->AddSample(*sample, slice);
    }
}

void PprofExporter::SetEndpoint(const std::string& runtimeId, uint64_t traceId, const std::string& endpoint)
{
}

bool PprofExporter::Export(ProfileTime& startTime, ProfileTime& endTime, bool lastCall)
{
    std::lock_guard lock(_builderLock);

    // Collect process samples (e.g. GcThreadsCpu) through the same per-entry loop.
    for (auto provider : _processSamplesProviders)
    {
        auto samplesEnumerator = provider->GetSamples();
        std::shared_ptr<Sample> sample;
        while (samplesEnumerator->MoveNext(sample))
        {
            auto const& allValues = sample->GetValues();
            for (auto& entry : _entries)
            {
                auto slice = std::span<const int64_t>(allValues.data() + entry.startIndex, entry.count);
                if (AllZero(slice)) continue;
                entry.builder->AddSample(*sample, slice);
            }
        }
    }

    std::vector<std::string> pprofs;
    for (auto& entry : _entries)
    {
        if (entry.builder->SamplesCount() != 0)
            pprofs.emplace_back(entry.builder->Build());
    }

    for (const auto& pprof : pprofs)
    {
        _sink->Export(std::move(pprof), startTime, endTime);
    }
    return true;
}

void PprofExporter::RegisterUpscaleProvider(IUpscaleProvider* provider) {};
void PprofExporter::RegisterUpscalePoissonProvider(IUpscalePoissonProvider* provider) {};

void PprofExporter::RegisterProcessSamplesProvider(ISamplesProvider* provider)
{
    _processSamplesProviders.push_back(provider);
};
void PprofExporter::RegisterApplication(std::string_view runtimeId) {};
void PprofExporter::RegisterGcSettingsProvider(IGcSettingsProvider* provider) {};
