//
// Created by root on 11/3/22.
//

#pragma once

#include "IExporter.h"
#include "Sample.h"
#include "TagsHelper.h"
#include "profile.pb.h"

#include <memory>
#include <mutex>
#include <string_view>
#include <tuple>

class PprofBuilder
{

public:
    PprofBuilder(std::vector<SampleValueType>& sampleTypeDefinitions, ProfileType type);

    void AddSample(const Sample& sample);
    int SamplesCount();
    std::tuple<std::string, ProfileType> Build();

private:
    int64_t AddString(const std::string_view& sv);
    int64_t AddLocation(int64_t functionName, int64_t moduleName);
    void Reset();

    std::mutex _lock;
    int _samplesCount = 0;
    perftools::profiles::Profile _profile;
    std::map<std::string_view, int64_t> _strings;
    std::map<std::pair<int64_t, int64_t>, int64_t> _locations;
    std::vector<SampleValueType>& _sampleTypeDefinitions;
    ProfileType _type;
};
