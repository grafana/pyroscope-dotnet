// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2025 Datadog, Inc.

#pragma once

#include <string>
#include <string_view>

#include "AsyncFrameKind.h"

// Recognises the C# async/await machinery in a frame name. Frame names are
// "Namespace!Type.Method" with generic arguments spelled inline, as built by
// FrameStore::FormatFrame. Both entry points are pure functions of the name; the frame store
// calls them once per method and caches the result, so nothing here runs per sample.
class AsyncFrames
{
public:
    // `assembly` is the frame's declaring assembly, as FrameStore::GetAssemblyName reports it.
    // It gates the plumbing markers only, since those are substring matches. State machine
    // detection is deliberately not gated, because a state machine belongs to the assembly that
    // declared the async method, never to the runtime.
    static AsyncFrameKind Classify(std::string_view frame, std::string_view assembly);

    // True for the assembly that holds the Task/async machinery. An unresolved (empty) assembly
    // is false, so a frame we cannot attribute keeps its place rather than being dropped.
    static bool IsRuntimeAssembly(std::string_view assembly);

    // "Ns!C.<M>d__4.MoveNext" -> "Ns!C.M", so an async method's body carries the same name as
    // its kickoff frame. Any other frame is returned unchanged.
    static std::string CanonicalName(std::string_view frame);
};
