// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2025 Datadog, Inc.

#include "RawSampleTransformer.h"

#include "AsyncScopeStore.h"
#include "OpSysTools.h"
#include "IAppDomainStore.h"
#include "IFrameStore.h"
#include "IRuntimeIdStore.h"

#include <chrono>
#include <string>
#include <string_view>
#include <utility>

std::shared_ptr<Sample> RawSampleTransformer::Transform(const RawSample& rawSample, std::vector<SampleValueTypeProvider::Offset> const& offsets)
{
    auto sample = std::make_shared<Sample>(rawSample.Timestamp, std::string_view(), rawSample.Stack.Size());
    Transform(rawSample, sample, offsets);
    return sample;
}

void RawSampleTransformer::Transform(const RawSample& rawSample, std::shared_ptr<Sample>& sample, std::vector<SampleValueTypeProvider::Offset> const& offsets)
{
    sample->Reset();

    auto runtimeId = _pRuntimeIdStore->GetId(rawSample.AppDomainId);

    sample->SetRuntimeId(runtimeId == nullptr ? std::string_view() : std::string_view(runtimeId));
    sample->SetTimestamp(rawSample.Timestamp);
    sample->SetTraceContext(rawSample.TraceContext); // todo handle

    for (auto &tag: rawSample.Tags.GetAll()) {
        sample->AddLabel(StringLabel{tag.first, tag.second});
    }

    // compute thread/appdomain details
    // SetAppDomainDetails(rawSample, sample); // pyroscope removed because of spaces in the label name and cardinality
    // SetThreadDetails(rawSample, sample); // pyroscope removed because of spaces in the label name and cardinality

    // compute symbols for frames
    SetStack(rawSample, sample);

    // allow inherited classes to add values and specific labels
    rawSample.OnTransform(sample, offsets);
}

void RawSampleTransformer::SetAppDomainDetails(const RawSample& rawSample, std::shared_ptr<Sample>& sample)
{
    auto appDomainName = _pAppDomainStore->GetName(rawSample.AppDomainId);
    sample->SetAppDomainName(std::string(appDomainName));
    sample->SetPid(OpSysTools::GetProcId());
}

void RawSampleTransformer::SetThreadDetails(const RawSample& rawSample, std::shared_ptr<Sample>& sample)
{
    // needed for tests
    if (rawSample.ThreadInfo == nullptr)
    {
        // find a way to skip thread details like for garbage collection where no managed threads are involved
        // --> if everything is empty

        if (
            (rawSample.TraceContext._currentLocalRootSpanId == 0) &&
            (rawSample.TraceContext._currentTraceIdHi == 0) &&
            (rawSample.TraceContext._currentTraceIdLo == 0) &&
            (rawSample.AppDomainId == 0) &&
            (rawSample.Stack.Size() == 0))
        {
            sample->SetThreadId(std::string("GC"));
            sample->SetThreadName(std::string("CLR thread (garbage collector)"));
            return;
        }

        sample->SetThreadId(std::string("<0> [#0]"));
        sample->SetThreadName(std::string("Managed thread (name unknown) [#0]"));

        return;
    }

    sample->SetThreadId(rawSample.ThreadInfo->GetProfileThreadId());
    sample->SetThreadName(rawSample.ThreadInfo->GetProfileThreadName());
}

void RawSampleTransformer::SetStack(const RawSample& rawSample, std::shared_ptr<Sample>& sample)
{
    // Frames arrive leaf-first. Every frame carries the async kind the frame store worked
    // out once for its method (AsyncFrameKind::UserCode for all of them when async frame
    // cleanup is off, which makes the two rules below no-ops).
    FrameInfoView previous{};
    FrameInfoView leaf{};
    bool hasLeaf = false;

    // Deal with fake stack frames like for garbage collections since the Stack will be empty
    for (auto const& instructionPointer : rawSample.Stack)
    {
        auto [isResolved, frame] = _pFrameStore->GetFrame(instructionPointer);

        if (!isResolved)
        {
            continue;
        }

        if (!hasLeaf)
        {
            leaf = frame;
            hasLeaf = true;
        }

        if (frame.AsyncKind == AsyncFrameKind::RuntimePlumbing)
        {
            continue;
        }

        // An async method contributes both a compiler-generated kickoff frame and the
        // state machine body that the kickoff starts, one directly above the other and
        // both named after the same method once canonicalised. Folding the kickoff away
        // keeps a sample that caught only the kickoff on the same node as one that caught
        // the body -- while leaving genuine recursion (two bodies) alone.
        auto const foldsIntoTheBodyBelowIt =
            previous.AsyncKind == AsyncFrameKind::StateMachineMoveNext &&
            frame.AsyncKind != AsyncFrameKind::StateMachineMoveNext &&
            previous.Frame == frame.Frame;

        if (foldsIntoTheBodyBelowIt)
        {
            continue;
        }

        sample->AddFrame(frame);
        previous = frame;
    }

    // A sample can be machinery all the way down: the inline-completion unwind recurses
    // deep enough to fill the callstack budget, which costs the outermost frames. Keeping
    // its leaf stops the sample from losing its stack altogether -- and the leaf is where
    // the sample actually was.
    if (hasLeaf && sample->GetCallstack().empty())
    {
        sample->AddFrame(leaf);
    }

    // The callstack is leaf-first, so the logical async parents belong at the end:
    // they extend the physical stack past its thread-pool root up to the scope that
    // caused this work (see AsyncScopeStore). Without them an `await` continuation
    // is a separate tree rooted at ThreadPoolWorkQueue.Dispatch instead of a
    // descendant of the endpoint that scheduled it.
    if (_pAsyncScopeStore != nullptr)
    {
        _pAsyncScopeStore->ForEachFrame(
            rawSample.AsyncScopeId,
            [&sample](FrameInfoView const& frame) { sample->AddFrame(frame); });
    }
}