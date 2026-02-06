//
// Created by root on 11/3/22.
//

#include "PprofBuilder.h"
#include "Log.h"
#include "Sample.h"
#include <tuple>

PprofBuilder::PprofBuilder(std::vector<SampleValueType>& sampleTypeDefinitions, ProfileType type) :
    _sampleTypeDefinitions(sampleTypeDefinitions),
    _type(type)
{
    Reset();
}

void PprofBuilder::AddSample(const Sample& sample)
{
    auto& values = sample.GetValues();
    assert(values.size() == _sampleTypeDefinitions.size());
    std::lock_guard<std::mutex> lock(this->_lock);
    auto* pSample = _profile.add_sample();
    for (auto const& frame : sample.GetCallstack())
    {
        auto moduleName = AddString(frame.ModuleName);
        auto functionName = AddString(frame.Frame);
        auto locId = AddLocation(functionName, moduleName);
        pSample->add_location_id(locId);
    }
    for (const auto& value : values)
    {
        pSample->add_value(value);
    }
    for (const auto& label : sample.GetLabels())
    {
        auto sl = std::get_if<StringLabel>(&label);
        if (sl)
        {
            auto* pLabel = pSample->add_label();
            pLabel->set_key(AddString(sl->first));
            pLabel->set_str(AddString(*sl->second.Get()));
        }
    }
    _samplesCount++;
}

int PprofBuilder::SamplesCount() {
    return _samplesCount;
}

std::tuple<std::string, ProfileType> PprofBuilder::Build()
{
    std::lock_guard<std::mutex> lock(this->_lock);
    auto res = _profile.SerializeAsString();
    Log::Debug("PprofBuilder samples: ", _samplesCount, ", serialized bytes: ", res.size());
    Reset();
    return std::tuple(std::move(res), _type);
}

int64_t PprofBuilder::AddString(const std::string_view& sv)
{
    auto it = _strings.find(sv);
    if (it != _strings.end())
    {
        return it->second;
    }
    int64_t id = (int64_t)_profile.string_table_size();
    assert(_strings.size() == id);
    _profile.add_string_table(sv.data(), sv.size());
    auto& back = _profile.string_table(id);
    _strings.insert(std::make_pair(std::string_view(back), id));
    return id;
}

int64_t PprofBuilder::AddLocation(int64_t functionName, int64_t moduleName)
{
    std::pair<int64_t, int64_t> loc(moduleName, functionName);
    auto it = _locations.find(loc);
    if (it != _locations.end())
    {
        return it->second;
    }
    int64_t id = (int64_t)_profile.location_size() + 1;
    assert(_profile.location_size() == _locations.size());
    assert(_profile.function_size() == _locations.size());
    auto* pFunction = _profile.add_function();
    pFunction->set_id(id);
    pFunction->set_name(functionName);
    pFunction->set_filename(moduleName);
    auto* pLocation = _profile.add_location();
    pLocation->set_id(id);
    auto* pLine = pLocation->add_line();
    pLine->set_function_id(id);
    _locations.insert(std::make_pair(loc, id));
    return id;
}

void PprofBuilder::Reset()
{
    _samplesCount = 0;
    _locations.clear();
    _strings.clear();
    _profile = perftools::profiles::Profile();
    AddString("");
    for (const auto& sampleType : _sampleTypeDefinitions)
    {
        auto* pSample = _profile.add_sample_type();
        pSample->set_unit(AddString(sampleType.Unit));
        pSample->set_type(AddString(sampleType.Name));
    }
    _profile.set_period(1);
    auto* pPeriodType = new perftools::profiles::ValueType();
    pPeriodType->set_type(AddString("cpu"));
    pPeriodType->set_unit(AddString("nanoseconds"));
    _profile.set_allocated_period_type(pPeriodType);
}
