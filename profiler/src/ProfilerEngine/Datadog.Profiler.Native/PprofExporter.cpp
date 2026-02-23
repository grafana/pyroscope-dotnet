//
// Created by root on 11/2/22.
//

#include "PprofExporter.h"
#include "ISamplesProvider.h"
#include "SamplesEnumerator.h"
#include "Log.h"
#include <signal.h>

static std::string ProfileTypeToName(ProfileType pt)
{
    switch (pt)
    {
        case ProfileType::Cpu:            return "process_cpu";
        case ProfileType::Wall:           return "wall";
        case ProfileType::Alloc:          return "alloc";
        case ProfileType::Heap:           return "memory";
        case ProfileType::Lock:           return "mutex";
        case ProfileType::Exception:      return "exceptions";
        case ProfileType::GcCpu:          return "gc_cpu";
        case ProfileType::GC:             return "goroutine";
        case ProfileType::GCStopTheWorld: return "block";
        case ProfileType::ThreadLifetime: return "threadtime";
        default:                          return "unknown";
    }
}

PprofExporter::PprofExporter(IApplicationStore* applicationStore,
                             std::shared_ptr<PProfExportSink> sink,
                             std::vector<SampleValueType> sampleTypeDefinitions) :
    _applicationStore(applicationStore),
    _sink(std::move(sink)),
    _sampleTypeDefinitions(std::move(sampleTypeDefinitions))
{
    // Build per-ProfileType builders: group sampleTypeDefinitions by profileType.
    // Each group gets a PprofBuilder with only its subset of types, and a globalOffset
    // so it reads the right slice from the full values vector.
    std::unordered_map<int32_t, std::pair<std::vector<SampleValueType>, size_t>> groups; // profileType -> (types, firstOffset)

    for (size_t i = 0; i < _sampleTypeDefinitions.size(); i++)
    {
        auto const& st = _sampleTypeDefinitions[i];
        int32_t key = static_cast<int32_t>(st.profileType);
        auto it = groups.find(key);
        if (it == groups.end())
        {
            groups.emplace(key, std::make_pair(std::vector<SampleValueType>{st}, i));
        }
        else
        {
            it->second.first.push_back(st);
        }
    }

    for (auto& [key, pair] : groups)
    {
        ProfileTypeEntry entry;
        entry.globalOffset = pair.second;
        entry.builder = std::make_unique<PprofBuilder>(std::move(pair.first), entry.globalOffset);
        _perProfileTypeBuilder.emplace(key, std::move(entry));
    }

    signal(SIGPIPE, SIG_IGN);
}

PProfExportSink::~PProfExportSink()
{
}

void PprofExporter::Add(std::shared_ptr<Sample> const& sample)
{
    int32_t key = static_cast<int32_t>(sample->GetProfileType());
    std::lock_guard lock(_perProfileTypeBuilderLock);
    auto it = _perProfileTypeBuilder.find(key);
    if (it != _perProfileTypeBuilder.end())
    {
        it->second.builder->AddSample(*sample);
    }
}

void PprofExporter::SetEndpoint(const std::string& runtimeId, uint64_t traceId, const std::string& endpoint)
{
}

bool PprofExporter::Export(ProfileTime& startTime, ProfileTime& endTime, bool lastCall)
{
    // Drain process samples providers into their respective per-ProfileType builders.
    {
        std::lock_guard lock(_processSamplesLock);
        for (auto provider : _processSamplesProviders)
        {
            auto samplesEnumerator = provider->GetSamples();
            std::shared_ptr<Sample> sample;
            while (samplesEnumerator->MoveNext(sample))
            {
                int32_t key = static_cast<int32_t>(sample->GetProfileType());
                std::lock_guard lock2(_perProfileTypeBuilderLock);
                auto it = _perProfileTypeBuilder.find(key);
                if (it != _perProfileTypeBuilder.end())
                {
                    it->second.builder->AddSample(*sample);
                }
            }
        }
    }

    std::vector<std::pair<std::string, ProfileType>> pprofs;
    {
        std::lock_guard lock(_perProfileTypeBuilderLock);
        for (auto& [key, entry] : _perProfileTypeBuilder)
        {
            if (entry.builder->SamplesCount() != 0)
            {
                pprofs.emplace_back(entry.builder->Build(), static_cast<ProfileType>(key));
            }
        }
    }

    for (auto& [pprof, profileType] : pprofs)
    {
        _sink->Export(std::move(pprof), startTime, endTime, ProfileTypeToName(profileType));
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
