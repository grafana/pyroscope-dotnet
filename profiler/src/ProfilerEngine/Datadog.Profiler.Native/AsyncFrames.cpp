// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2025 Datadog, Inc.

#include "AsyncFrames.h"

#include <cctype>

namespace {

// Task/async machinery, matched as substrings of "Type.Method".
//
// The blocking waits (Task.Wait, Task.SpinThenBlockingWait, ManualResetEventSlim.Wait) are
// deliberately absent: sync-over-async is something you want to find in a profile, not
// machinery to hide.
constexpr std::string_view PlumbingMarkers[] = {
    // The continuation box: one generic instantiation per async method.
    "AsyncStateMachineBox",

    // The builders. Matched without a trailing '.', because a builder for a
    // Task<T>-returning method is itself generic and its arguments sit between the type
    // name and the method: "AsyncTaskMethodBuilder<T>.Start<...>". Spelled out rather than
    // matched on "MethodBuilder", which would also catch System.Reflection.Emit.MethodBuilder.
    "AsyncTaskMethodBuilder",
    "AsyncValueTaskMethodBuilder",
    "AsyncVoidMethodBuilder",
    "AsyncMethodBuilderCore",
    "AsyncIteratorMethodBuilder",

    // Where a queued continuation resumes. Only the delivery path itself: the pool's own
    // bookkeeping (PortableThreadPool.WorkerThread.*, GateThread, the work-stealing queue)
    // burns real CPU when the pool is churning, and thread-pool starvation is something you
    // go to a profiler to find.
    "ThreadPoolWorkQueue.Dispatch",
    "ExecutionContext.Run",

    // The inline-completion unwind: when an awaited task completes and its continuation runs
    // synchronously on the completing thread, this chain nests the awaited operation above
    // its own awaiter, deep enough to fill the whole callstack budget.
    "TaskContinuation",
    "ContinuationWrapper",
    "UnwrapPromise",
    "Task.RunContinuations",
    "Task.FinishStageTwo",
    "Task.FinishStageThree",
    "Task.FinishContinuations",
    "Task.ExecuteWithThreadLocal",
    "Task.ExecuteEntry",
    "Task.ExecuteFromThreadPool",

    // The delegate hand-off between the pool and the work it is delivering: a Task.Run body
    // lands under InnerInvoke, and ScheduleAndStart is the start side. Spelled with the type
    // name so an unrelated InnerInvoke elsewhere in the runtime assembly is not caught.
    "Task.InnerInvoke",
    "Task.ScheduleAndStart",
    "TryExecuteTaskInline",
    "TryRunInline",
    "InlineIfPossibleOrElseQueue",
    "RunOrScheduleAction",

    // Runtime async (.NET 12+) and the runtime's own async profiler. A runtime-async method has
    // no compiler state machine, so the markers above never match for one and these take their
    // place: DispatchContinuations is the flat resume loop, the other two are the instrumented
    // clones of the classic-async dispatch path. The Continuation_Wrapper_N frames need no
    // marker, their declaring type is AsyncProfiler.ContinuationWrapper.
    "DispatchContinuations",
    "AsyncStateMachineDispatcher",
    "InstrumentedMoveNext",
    "MoveNextAsDispatcher",

    // Awaiting itself. Generic for the same reason: "TaskAwaiter<T>.GetResult".
    "TaskAwaiter",
    "ValueTaskAwaiter",
    "ConfiguredTaskAwaitable",
    "ConfiguredValueTaskAwaitable",
};

// Every marker above lives in the runtime's own assembly, so requiring it costs no coverage
// and removes the whole class of application-code false positives.
constexpr std::string_view RuntimeAssemblies[] = {
    "System.Private.CoreLib", // .NET Core / .NET 5+
    "mscorlib",               // .NET Framework
};

constexpr std::string_view MoveNextSuffix = ".MoveNext";
constexpr std::string_view StateMachineInfix = "d__";

// Strips a trailing balanced "<...>" group, if there is one. Nested groups are counted,
// so "<System.Collections.Generic.List<System.Int32>>" is removed as a whole.
bool TryStripTrailingGenerics(std::string_view& name)
{
    if (name.empty() || name.back() != '>')
    {
        return false;
    }

    std::size_t depth = 0;
    for (std::size_t i = name.size(); i > 0; i--)
    {
        auto const c = name[i - 1];
        if (c == '>')
        {
            depth++;
        }
        else if (c == '<')
        {
            depth--;
            if (depth == 0)
            {
                name = name.substr(0, i - 1);
                return true;
            }
        }
    }

    return false;
}

// Recognises the compiler's async state machine type: "Prefix.<Method>d__N" with an optional
// generic argument list, as in "HttpProtocol.<ProcessRequests>d__237<T>". Plenty of frames end
// in ".MoveNext" without being one of these -- every iterator does -- so the "<Method>d__N"
// shape is what we key on rather than the method name.
bool TryParseStateMachine(std::string_view frame, std::string_view& prefix, std::string_view& method)
{
    if (frame.size() <= MoveNextSuffix.size() || frame.substr(frame.size() - MoveNextSuffix.size()) != MoveNextSuffix)
    {
        return false;
    }

    auto type = frame.substr(0, frame.size() - MoveNextSuffix.size());

    // A generic async method's state machine is itself generic.
    TryStripTrailingGenerics(type);

    // "d__N": strip the ordinal, then the marker. An ordinal is required, so a type
    // ending in a bare "d" (and anything else) is not one of these.
    auto const sizeBeforeOrdinal = type.size();
    while (!type.empty() && std::isdigit(static_cast<unsigned char>(type.back())) != 0)
    {
        type.remove_suffix(1);
    }

    if (type.size() == sizeBeforeOrdinal || type.size() <= StateMachineInfix.size() ||
        type.substr(type.size() - StateMachineInfix.size()) != StateMachineInfix)
    {
        return false;
    }

    type.remove_suffix(StateMachineInfix.size());

    // What is left is "Prefix.<Method>", where Method may itself contain angle brackets
    // when the async method is a local function.
    auto const withoutMethod = type;
    if (!TryStripTrailingGenerics(type))
    {
        return false;
    }

    prefix = type;
    method = withoutMethod.substr(type.size() + 1, withoutMethod.size() - type.size() - 2);
    return !method.empty();
}

} // namespace

bool AsyncFrames::IsRuntimeAssembly(std::string_view assembly)
{
    for (auto const& candidate : RuntimeAssemblies)
    {
        if (assembly == candidate)
        {
            return true;
        }
    }

    return false;
}

AsyncFrameKind AsyncFrames::Classify(std::string_view frame, std::string_view assembly)
{
    // The markers are substrings, so they are only trustworthy inside the assembly that
    // declares the machinery: elsewhere "TaskContinuation" is just as likely to be an
    // application type, and TryExecuteTaskInline a method the user wrote.
    if (IsRuntimeAssembly(assembly))
    {
        for (auto const& marker : PlumbingMarkers)
        {
            if (frame.find(marker) != std::string_view::npos)
            {
                return AsyncFrameKind::RuntimePlumbing;
            }
        }
    }

    // Not gated: a state machine is declared by the assembly that wrote the async method, so
    // gating this would disable the kickoff fold for every async method a user writes.
    std::string_view prefix;
    std::string_view method;
    if (TryParseStateMachine(frame, prefix, method))
    {
        return AsyncFrameKind::StateMachineMoveNext;
    }

    return AsyncFrameKind::UserCode;
}

std::string AsyncFrames::CanonicalName(std::string_view frame)
{
    std::string_view prefix;
    std::string_view method;
    if (!TryParseStateMachine(frame, prefix, method))
    {
        return std::string(frame);
    }

    std::string canonical;
    canonical.reserve(prefix.size() + method.size());
    canonical.append(prefix).append(method);
    return canonical;
}
