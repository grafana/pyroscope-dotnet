//
// Created by root on 11/3/22.
//

#pragma once

#include "Sample.h"
#include "TagsHelper.h"
#include "google/v1/profile.pb.h"

#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>

class PprofBuilder
{

public:
    PprofBuilder(std::vector<SampleValueType> sampleTypeDefinitions);

    void AddSample(const Sample& sample, std::span<const int64_t> values);
    std::optional<std::string> Build();

private:
    int64_t AddString(const std::string_view& sv);
    int64_t AddLocation(int64_t functionName, int64_t moduleName);
    void Reset();

    std::mutex _lock;
    int _samplesCount = 0;
    google::v1::Profile _profile;
    std::map<std::string_view, int64_t> _strings;
    std::map<std::pair<int64_t, int64_t>, int64_t> _locations;
    std::vector<SampleValueType> _sampleTypeDefinitions;

};
