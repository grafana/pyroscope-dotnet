//
// Created by root on 11/3/22.
//

#include "PprofBuilder.h"
#include "IExporter.h"
#include "Log.h"

namespace {
constexpr std::string_view ClassLeafPrefix = "cls:";
auto constexpr spanIDLabelName   = "span_id";
auto constexpr traceIDLabelName  = "trace_id";

}

PprofBuilder::PprofBuilder(std::vector<SampleValueType> sampleTypeDefinitions) :
    _sampleTypeDefinitions(std::move(sampleTypeDefinitions))
{
    Reset();
}

void PprofBuilder::AddSample(const Sample& sample, std::span<const int64_t> values)
{
    assert(values.size() == _sampleTypeDefinitions.size());
    std::lock_guard lock(this->_lock);
    auto* pSample = _profile.add_sample();
    auto addLocation = [&](std::string_view frame, std::string_view module) {
        const auto moduleNameStringId = AddString(module);
        const auto functionNameStringId = AddString(frame);
        const auto locId = AddLocation(functionNameStringId, moduleNameStringId);
        pSample->add_location_id(locId);
    };
    auto addFrame = [&](const FrameInfoView& frame) {
        addLocation(frame.Frame, frame.ModuleName);
    };
    AddTraceContext(sample, pSample);
    if (auto frame = sample.GetLeafFrame(); !frame.empty())
    {
        _scratchBuffer.clear();
        _scratchBuffer.reserve(ClassLeafPrefix.size() + frame.size());
        _scratchBuffer.append(ClassLeafPrefix);
        _scratchBuffer.append(frame);
        addLocation(_scratchBuffer, {});
    }
    for (auto const& frame : sample.GetCallstack())
    {
        addFrame(frame);
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

std::string PprofBuilder::Build(ProfileTime& startTime, ProfileTime& endTime)
{
    std::lock_guard<std::mutex> lock(this->_lock);
    if (_samplesCount == 0)
    {
        return {};
    }
    auto startNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(startTime.time_since_epoch()).count();
    auto durationNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
    _profile.set_time_nanos(startNanos);
    _profile.set_duration_nanos(durationNanos);
    auto res = _profile.SerializeAsString();
    Log::Debug("PprofBuilder samples: ", _samplesCount, ", serialized bytes: ", res.size());
    Reset();
    return std::move(res);
}


int64_t PprofBuilder::AddHexString(std::span<const std::byte> src)
{
    static constexpr char lut[] = "0123456789abcdef";

    _scratchBuffer.clear();
    _scratchBuffer.reserve(src.size() * 2);

    for (std::byte b : src)
    {
        uint8_t v = std::to_integer<uint8_t>(b);
        _scratchBuffer.push_back(lut[v >> 4]);
        _scratchBuffer.push_back(lut[v & 0xF]);
    }

    return AddString(std::string_view{_scratchBuffer});

}
int64_t PprofBuilder::AddString(const std::string_view& keyNotOwned)
{
    if (const auto it = _strings.find(keyNotOwned); it != _strings.end())
    {
        return it->second;
    }
    int64_t id = _profile.string_table_size();
    auto *back = _profile.add_string_table();
    *back = std::string{keyNotOwned}; // make a copy
    auto key = std::string_view{*back}; // key view into pprof string table entry
    _strings.insert(std::make_pair(key, id));
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
    _profile = google::v1::Profile();
    AddString("");
    _span_id_str_index = AddString(spanIDLabelName);
    _trace_id_str_index = AddString(traceIDLabelName);
    for (const auto& sampleType : _sampleTypeDefinitions)
    {
        auto* pSample = _profile.add_sample_type();
        pSample->set_unit(AddString(sampleType.Unit));
        pSample->set_type(AddString(sampleType.Name));
    }
    _profile.set_period(1);
    auto* pPeriodType = new google::v1::ValueType();
    bool isMemoryProfile = !_sampleTypeDefinitions.empty() &&
                           (_sampleTypeDefinitions[0].Type == ProfileType::Alloc ||
                            _sampleTypeDefinitions[0].Type == ProfileType::AllocFramework ||
                            _sampleTypeDefinitions[0].Type == ProfileType::Heap);
    if (isMemoryProfile)
    {
        pPeriodType->set_type(AddString("space"));
        pPeriodType->set_unit(AddString("bytes"));
    }
    else
    {
        pPeriodType->set_type(AddString("cpu"));
        pPeriodType->set_unit(AddString("nanoseconds"));
    }
    _profile.set_allocated_period_type(pPeriodType);
}


void PprofBuilder::AddTraceContext(const Sample& sample, google::v1::Sample* pSample)
{
    if (auto ctx = sample.GetTraceContext(); ctx._currentLocalRootSpanId != 0)
    {
        uint64_t spanId{ctx._currentLocalRootSpanId};
        uint64_t traceId[2]{ctx._currentTraceIdHi, ctx._currentTraceIdLo};
        int64_t spanIdStr{AddHexString(std::as_bytes(std::span{&spanId, 1}))};
        int64_t traceIdStr{AddHexString(std::as_bytes(std::span{&traceId, 2}))};//todo this should be caught by the test
        {
            auto* l = pSample->add_label();
            l->set_key(_span_id_str_index);
            l->set_str(spanIdStr);
        }
        {
            auto* l = pSample->add_label();
            l->set_key(_trace_id_str_index);
            l->set_str(traceIdStr);
        }
    }
}