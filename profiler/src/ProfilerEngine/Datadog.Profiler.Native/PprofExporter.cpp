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
                             std::unordered_map<ProfileType, std::vector<SampleValueType>> profileTypeDefinitions) :
    _applicationStore(applicationStore),
    _sink(std::move(sink)),
    _profileTypeDefinitions(std::move(profileTypeDefinitions))
{
    signal(SIGPIPE, SIG_IGN);
}

PProfExportSink::~PProfExportSink()
{
}

void PprofExporter::Add(std::shared_ptr<Sample> const& sample)
{
    auto* builder = GetOrCreateBuilder(sample->GetProfileType());
    if (builder != nullptr)
    {
        builder->AddSample(*sample);
    }
}

void PprofExporter::SetEndpoint(const std::string& runtimeId, uint64_t traceId, const std::string& endpoint)
{
}

bool PprofExporter::Export(ProfileTime& startTime, ProfileTime& endTime, bool lastCall)
{
    std::vector<std::string> pprofs;
    {
        std::lock_guard lock(_buildersLock);
        for (auto& kv : _perProfileTypeBuilder)
        {
            if (kv.second->SamplesCount() != 0)
            {
                pprofs.emplace_back(kv.second->Build());
            }
        }
    }

    for (const auto& pprof : pprofs)
    {
        _sink->Export(std::move(pprof), startTime, endTime);
    }
    return true;
}

PprofBuilder* PprofExporter::GetOrCreateBuilder(ProfileType profileType)
{
    std::lock_guard lock(_buildersLock);
    auto it = _perProfileTypeBuilder.find(profileType);
    if (it != _perProfileTypeBuilder.end())
    {
        return it->second.get();
    }

    auto defIt = _profileTypeDefinitions.find(profileType);
    if (defIt == _profileTypeDefinitions.end())
    {
        return nullptr;
    }

    auto instance = std::make_unique<PprofBuilder>(defIt->second);
    auto* ptr = instance.get();
    _perProfileTypeBuilder.emplace(profileType, std::move(instance));
    return ptr;
}

void PprofExporter::RegisterUpscaleProvider(IUpscaleProvider* provider) {};
void PprofExporter::RegisterUpscalePoissonProvider(IUpscalePoissonProvider* provider) {};
void PprofExporter::RegisterProcessSamplesProvider(ISamplesProvider* provider) {};
void PprofExporter::RegisterApplication(std::string_view runtimeId) {};
void PprofExporter::RegisterGcSettingsProvider(IGcSettingsProvider* provider) {};
