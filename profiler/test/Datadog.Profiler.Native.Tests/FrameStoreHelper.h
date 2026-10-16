// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "IFrameStore.h"

class FrameStoreHelper : public IFrameStore
{
public:
    FrameStoreHelper(bool isManaged, std::string prefix, size_t count);

    // Resolves instruction pointers 1..frames.size() to the given names, classified
    // and canonicalised the way FrameStore does when async frame cleanup is enabled.
    // Takes the names leaf-first, which is the order the sampler collects them in.
    explicit FrameStoreHelper(std::vector<std::string> const& frames);

public:
    // Inherited via IFrameStore
    std::pair<bool, FrameInfoView> GetFrame(uintptr_t instructionPointer) override;
    bool GetTypeName(ClassID classId, std::string& name) override;
    bool GetTypeName(ClassID classId, TypeNameView& name) override;

    // IMemoryFootprintProvider
    size_t GetMemorySize() const override { return 0; }
    void LogMemoryBreakdown() const override {}

private:
    struct FrameInfo
    {
    public:
        std::string ModuleName;
        std::string Frame;
        std::string_view Filename;
        std::uint32_t StartLine;
        AsyncFrameKind AsyncKind = AsyncFrameKind::UserCode;

        operator FrameInfoView() const
        {
            return {ModuleName, Frame, Filename, StartLine, AsyncKind};
        }
    };
    std::unordered_map<uintptr_t, std::pair<bool, FrameInfo>> _mapping;

    // Inherited via IFrameStore
};
