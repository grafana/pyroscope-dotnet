// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include <sstream>
#include "AsyncFrames.h"
#include "FrameStoreHelper.h"

namespace {

// Fixtures are given as frame names alone, but AsyncFrames::Classify gates the plumbing
// markers on the declaring assembly. Derive a plausible one rather than claiming everything
// is the runtime: a "System.*" namespace stands in for System.Private.CoreLib, and anything
// else is treated as its own assembly, which is what a real application frame looks like.
std::string AssemblyFor(std::string const& frame)
{
    if (frame.rfind("System.", 0) == 0)
    {
        return "System.Private.CoreLib";
    }

    return frame.substr(0, frame.find('!'));
}

} // namespace

FrameStoreHelper::FrameStoreHelper(bool isManaged, std::string prefix, size_t count)
{
    // build automatically a mapping
    //    number --> { isManaged, "module #number", "prefix #number" }
    // with number going from 1 to count
    for (size_t i = 1; i <= count; i++)
    {
        std::stringstream frameBuilder;
        frameBuilder << prefix << " #" << i;

        std::stringstream moduleBuilder;
        moduleBuilder << "module #" << i;

        _mapping[i] = {isManaged, {moduleBuilder.str(), frameBuilder.str(), "", 0}};
    }
}

FrameStoreHelper::FrameStoreHelper(std::vector<std::string> const& frames)
{
    for (size_t i = 0; i < frames.size(); i++)
    {
        std::stringstream moduleBuilder;
        moduleBuilder << "module #" << (i + 1);

        _mapping[i + 1] = {true,
                           {moduleBuilder.str(),
                            AsyncFrames::CanonicalName(frames[i]),
                            "",
                            0,
                            AsyncFrames::Classify(frames[i], AssemblyFor(frames[i]))}};
    }
}

std::pair<bool, FrameInfoView> FrameStoreHelper::GetFrame(uintptr_t instructionPointer)
{
    static std::string UnknownModuleName = "module???";
    static std::string UnknownFunctionName = "frame???";

    auto item = _mapping.find(instructionPointer);
    if (item != _mapping.end())
    {
        return item->second;
    }

    return {true, {UnknownModuleName, UnknownFunctionName, "", 0}};
}


bool FrameStoreHelper::GetTypeName(ClassID classId, std::string& name)
{
    name = "";
    return false;
}

bool FrameStoreHelper::GetTypeName(ClassID classId, TypeNameView& name)
{
    name = {};
    return false;
}
