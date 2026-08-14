// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once
#include "cor.h"
#include "corprof.h"

#include "IMemoryFootprintProvider.h"

#include <cstdint>
#include <string>
#include <string_view>

struct FrameInfoView
{
public:
    std::string_view ModuleName;
    std::string_view Frame;
    std::string_view Filename;
    std::uint32_t StartLine;
};

// Non-owning view over a type name whose storage is owned by a frame store and
// lives as long as the store itself (i.e. the process lifetime in production).
// Only IFrameStore implementations can create non-empty instances, so holding a
// TypeNameView in a sample is safe by construction: it is a compile-time error
// to wrap a temporary or a reused buffer (e.g. the per-thread ETW type-name
// buffer) in one.
class TypeNameView
{
public:
    TypeNameView() = default;

    // Deliberately not an implicit conversion: leaving the safe TypeNameView world
    // must be visible at the call site (serialization boundaries only)
    std::string_view AsStringView() const
    {
        return _view;
    }

    bool IsEmpty() const
    {
        return _view.empty();
    }

private:
    friend class IFrameStore;

    explicit TypeNameView(std::string_view view) :
        _view{view}
    {
    }

    std::string_view _view;
};

class IFrameStore : public IMemoryFootprintProvider
{
public:
    virtual ~IFrameStore() = default;

    // return
    //  - true if managed frame
    virtual std::pair<bool, FrameInfoView> GetFrame(uintptr_t instructionPointer) = 0;
    // On failure (returns false), name is set to empty
    virtual bool GetTypeName(ClassID classId, std::string& name) = 0;
    virtual bool GetTypeName(ClassID classId, TypeNameView& name) = 0;

protected:
    // Implementations must only wrap storage they own for their whole lifetime.
    static TypeNameView MakeTypeNameView(std::string_view view)
    {
        return TypeNameView{view};
    }
};
