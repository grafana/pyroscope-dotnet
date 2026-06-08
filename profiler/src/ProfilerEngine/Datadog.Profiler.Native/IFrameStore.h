// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once
#include "cor.h"
#include "corprof.h"

#include "FrameInfoView.h"
#include "IMemoryFootprintProvider.h"
#include <cstdint>
#include <string>

class IFrameStore : public IMemoryFootprintProvider
{
public:
    ~IFrameStore() override = default;

    // return
    //  - true if managed frame
    virtual std::pair<bool, FrameInfoView> GetFrame(uintptr_t instructionPointer) = 0;
    virtual bool GetTypeName(ClassID classId, std::string& name) = 0;
    virtual bool GetTypeName(ClassID classId, std::string_view& name, const std::u16string_view& name_fallback) = 0;
};
