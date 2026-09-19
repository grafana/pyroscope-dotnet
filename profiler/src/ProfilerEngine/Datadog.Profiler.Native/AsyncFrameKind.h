// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2025 Datadog, Inc.

#pragma once

// How a resolved frame relates to the C# async/await machinery. Computed once per method by
// AsyncFrames and cached alongside the frame's name in the frame store, then applied per sample
// by RawSampleTransformer::SetStack.
enum class AsyncFrameKind
{
    // An ordinary frame. Kept as it is.
    UserCode,

    // Task/async runtime machinery with no user code in it: the continuation box, thread-pool
    // dispatch, the builder's Start, the inline-completion unwind. These appear inconsistently,
    // depending on what the JIT inlined for a given instantiation, so two samples on the same
    // logical path land in different flamegraph nodes. Dropped when async frame cleanup is on;
    // their self time falls to the caller that ran them.
    RuntimePlumbing,

    // The body of an async method: "Class.<Method>d__4.MoveNext". Kept, but its canonical name
    // is "Class.Method", which is also what the compiler-generated kickoff frame is called, so
    // the two merge into one node instead of stacking as a near-duplicate pair.
    StateMachineMoveNext,
};
