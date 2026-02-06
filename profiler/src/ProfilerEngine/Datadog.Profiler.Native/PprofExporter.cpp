//
// Created by root on 11/2/22.
//

#include "PprofExporter.h"
#include "IExporter.h"
#include "ISamplesProvider.h"
#include "Sample.h"
#include "SamplesEnumerator.h"
#include "Log.h"
#include <signal.h>
#include <tuple>

PprofExporter::PprofExporter(std::shared_ptr<PProfExportSink> sink,
                             std::vector<SampleValueType> sampleTypeDefinitions) :
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

    _processSamplesBuilder = std::make_unique<PprofBuilder>(_processSampleTypeDefinitions, ProfileType::PROCESS);
    signal(SIGPIPE, SIG_IGN);
}

PProfExportSink::~PProfExportSink()
{
}

void PprofExporter::Add(std::shared_ptr<Sample> const& sample)
{
    bool found = false;
    ProfileType type = GetPprofType(sample, found);
    if (!found)
    {
        Log::Error("unknown type");// todo do not commit/merge
        return;
    }
    GetPprofBuilder(type)
        .AddSample(*sample);
}

void PprofExporter::SetEndpoint(const std::string& runtimeId, uint64_t traceId, const std::string& endpoint)
{
}

bool PprofExporter::Export(ProfileTime& startTime, ProfileTime& endTime, bool lastCall)
{
    std::vector<std::tuple<std::string, ProfileType>> pprofs;
    {
        std::lock_guard lock(_perProfileTypeBuilderLock);
        for (auto& builder : _perProfileTypeBuilder)
        {
            if (builder.second->SamplesCount() != 0)
            {
                pprofs.emplace_back(builder.second->Build());
            }
        }
    }

    {
        std::lock_guard lock(_processSamplesLock);
        for (auto* provider : _processSamplesProviders)
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
        _sink->Export(std::move(std::get<0>(pprof)), std::get<1>(pprof), startTime, endTime);
    }
    return true;
}

ProfileType PprofExporter::GetPprofType(std::shared_ptr<Sample> const& sample, bool& found)
{
    assert(sample->GetValues().size() == this->_sampleTypeDefinitions.size());
    if (sample->GetValues().size() != this->_sampleTypeDefinitions.size()) {
        return ProfileType::CPU; // not found
    }
    for (int i = 0; i < _sampleTypeDefinitions.size(); ++i) {
        if (sample->GetValues()[i] != 0) {
            found = true;
            return _sampleTypeDefinitions[i].ProfileType;
        }
    }
    return ProfileType::CPU;// not found
}

PprofBuilder& PprofExporter::GetPprofBuilder(ProfileType type)
{
    std::lock_guard lock(_perProfileTypeBuilderLock);
    auto it = _perProfileTypeBuilder.find(type);
    if (it != _perProfileTypeBuilder.end())
    {
        return *it->second;
    }
    auto instance = std::make_unique<PprofBuilder>(_sampleTypeDefinitions, type); // todo too many zeros - only pass and use subset of sample types
    auto res = _perProfileTypeBuilder.emplace(type, std::move(instance));
    return *res.first->second;
}

void PprofExporter::RegisterUpscaleProvider(IUpscaleProvider* provider) {};
void PprofExporter::RegisterUpscalePoissonProvider(IUpscalePoissonProvider* provider) {};

void PprofExporter::RegisterProcessSamplesProvider(ISamplesProvider* provider)
{
    std::lock_guard lock(_processSamplesLock);
    _processSamplesProviders.push_back(provider);
};
void PprofExporter::RegisterApplication(std::string_view runtimeId) {};
