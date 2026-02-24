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
        case ProfileType::GC:             return "gc";
        case ProfileType::GCStopTheWorld: return "gc_stw";
        case ProfileType::ThreadLifetime: return "thread_lifetime";
        default:                          return "unknown";
    }
}

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

void PprofExporter::AddSampleToBuilder(std::shared_ptr<Sample> const& sample)
{
    int32_t key = static_cast<int32_t>(sample->GetProfileType());
    std::lock_guard lock(_perProfileTypeBuilderLock);
    auto it = _perProfileTypeBuilder.find(key);
    if (it == _perProfileTypeBuilder.end())
    {
        auto const* sampleValueTypes = sample->GetSampleValueTypes();
        if (sampleValueTypes == nullptr)
            return;
        std::vector<SampleValueType> types(*sampleValueTypes);
        ProfileTypeEntry entry;
        entry.builder = std::make_unique<PprofBuilder>(std::move(types));
        it = _perProfileTypeBuilder.emplace(key, std::move(entry)).first;
    }
    it->second.builder->AddSample(*sample);
}

void PprofExporter::Add(std::shared_ptr<Sample> const& sample)
{
    AddSampleToBuilder(sample);
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
                AddSampleToBuilder(sample);
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
