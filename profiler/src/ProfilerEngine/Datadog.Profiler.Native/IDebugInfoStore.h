// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once

// from dotnet coreclr includes
#include "cor.h"
#include "corprof.h"
// end

#include "IMemoryFootprintProvider.h"

#include <string_view>
#include <cstdint>
#include <vector>

struct SequencePointInfo
{
public:
    std::uint32_t ILOffset;
    std::uint32_t StartLine;
};

struct SymbolDebugInfo
{
public:
    std::string_view File;
    std::uint32_t StartLine = 0;

    // non-hidden sequence points of the method, sorted by IL offset; used to resolve the
    // source line of a sampled instruction. Only populated when line numbers are enabled.
    std::vector<SequencePointInfo> SequencePoints;
};

class IDebugInfoStore : public IMemoryFootprintProvider
{
public:
    virtual ~IDebugInfoStore() = default;
    virtual SymbolDebugInfo Get(ModuleID moduleId, mdMethodDef methodDef) = 0;
};