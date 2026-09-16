// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2025 Datadog, Inc.

#include "gtest/gtest.h"

#include <string>
#include <string_view>

#include "AsyncFrames.h"

// Frame names come out of FrameStore::FormatFrame as "Namespace!Type.Method", with
// generic arguments spelled inline. The samples below are real frame names taken from
// a profiled ASP.NET Core service.
//
// The second argument to Classify is the frame's declaring assembly, which gates the
// plumbing markers: they are substring matches, so they are only trustworthy inside the
// assembly that actually declares the machinery. Note this is the assembly, not the
// namespace in the frame -- a CoreLib frame can carry a user type in its generic
// arguments, as the continuation box below does.
constexpr std::string_view CoreLib = "System.Private.CoreLib";

TEST(AsyncFramesTest, TheContinuationBoxIsRuntimePlumbing)
{
    // One generic instantiation per async method, so these never merge in a flamegraph.
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!AsyncTaskMethodBuilder"
                                    ".AsyncStateMachineBox<System.Threading.Tasks.VoidTaskResult, "
                                    "Microsoft.AspNetCore.Session!SessionMiddleware.<Invoke>d__8>.MoveNext",
                                    CoreLib));
}

TEST(AsyncFramesTest, TheContinuationDeliveryPathIsRuntimePlumbing)
{
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading!ThreadPoolWorkQueue.Dispatch", CoreLib));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading!ExecutionContext.RunFromThreadPoolDispatchLoop", CoreLib));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading!ExecutionContext.RunInternal", CoreLib));
}

TEST(AsyncFramesTest, ThreadPoolBookkeepingIsKeptBecauseItIsRealWork)
{
    // These are not on the path from a thread root to a resumed continuation -- they are the
    // pool managing itself, and they burn measurable CPU when it is churning. Thread-pool
    // starvation is exactly the kind of thing you go to a profiler to find, so the worker
    // thread root and the queue mechanics stay.
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Threading!PortableThreadPool.WorkerThread.WorkerThreadStart", CoreLib));
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Threading!PortableThreadPool.WorkerThread.MaybeAddWorkingWorker", CoreLib));
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Threading!PortableThreadPool.GateThread.GateThreadStart", CoreLib));
    EXPECT_EQ(AsyncFrameKind::UserCode, AsyncFrames::Classify("System.Threading!ThreadPoolWorkQueue.Enqueue", CoreLib));
    EXPECT_EQ(AsyncFrameKind::UserCode, AsyncFrames::Classify("System.Threading!ThreadPoolWorkQueue.Dequeue", CoreLib));
}

TEST(AsyncFramesTest, TheBuilderStartFramesAreRuntimePlumbing)
{
    // These appear only when the JIT did not inline them away, which is why the same
    // logical path lands in two different flamegraph nodes.
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!AsyncTaskMethodBuilder"
                                    ".Start<Microsoft.AspNetCore.Diagnostics!DeveloperExceptionPageMiddlewareImpl.<Invoke>d__14>",
                                    CoreLib));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!AsyncMethodBuilderCore"
                                    ".Start<Microsoft.AspNetCore.Diagnostics!DeveloperExceptionPageMiddlewareImpl.<Invoke>d__14>",
                                    CoreLib));
}

TEST(AsyncFramesTest, TheGenericBuildersAreRuntimePlumbingToo)
{
    // A builder for a Task<T>-returning method is itself generic, so its generic arguments
    // sit between the type name and the method: "AsyncTaskMethodBuilder<T>.Start<...>".
    // Matching on "AsyncTaskMethodBuilder." misses every one of them.
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!AsyncTaskMethodBuilder<System.__Canon>"
                                    ".Start<Pyroscope!LabelsWrapper.<Do>d__3>",
                                    CoreLib));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!AsyncTaskMethodBuilder<System.Threading.Tasks.VoidTaskResult>"
                                    ".AwaitUnsafeOnCompleted<System.Runtime.CompilerServices!ValueTaskAwaiter, "
                                    "Microsoft.AspNetCore.Server.Kestrel.Transport.Sockets.Internal!SocketConnection.<DoSend>d__28>",
                                    CoreLib));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!TaskAwaiter<System.__Canon>.GetResult", CoreLib));

    // The pooled builder ValueTask-returning methods use is a distinct type name, and so is
    // the one `async void` uses.
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!PoolingAsyncValueTaskMethodBuilder<System.Int32>"
                                    ".Start<System.Net.Security!SslStream.<ReadAsyncInternal>d__9>",
                                    CoreLib));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!AsyncVoidMethodBuilder"
                                    ".Start<Microsoft.Data.SqlClient.ManagedSni!SniPacket.<WriteToStreamAsync>d__31>",
                                    CoreLib));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!PoolingAsyncValueTaskMethodBuilder<System.Int32>"
                                    ".GetStateMachineBox<System.Net.Security!SslStream.<EnsureFullTlsFrameAsync>d__1>",
                                    CoreLib));
}

TEST(AsyncFramesTest, RuntimeHelpersAreKeptBecauseTheyAreRealWork)
{
    // Also in System.Runtime.CompilerServices, and nothing to do with async: string
    // interpolation and the JIT's static/generic lookup helpers do measurable work.
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Runtime.CompilerServices!DefaultInterpolatedStringHandler.AppendLiteral", CoreLib));
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Runtime.CompilerServices!StaticsHelpers.GetGCStaticBase", CoreLib));
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Runtime.CompilerServices!VirtualDispatchHelpers.VirtualFunctionPointer", CoreLib));
}

TEST(AsyncFramesTest, TheInlineCompletionUnwindIsRuntimePlumbing)
{
    // The recursion that fills the 1024-frame callstack budget on its own.
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading.Tasks!Task.RunContinuations", CoreLib));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading.Tasks!UnwrapPromise<System.Boolean>.TrySetFromTask", CoreLib));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading.Tasks!TaskContinuation.InlineIfPossibleOrElseQueue", CoreLib));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading.Tasks!ThreadPoolTaskScheduler.TryExecuteTaskInline", CoreLib));
}

TEST(AsyncFramesTest, TheDelegateInvocationAndScheduleHelpersAreRuntimePlumbing)
{
    // Both sit between the pool's dispatch and the work it is delivering. PerfView removes
    // them too: InnerInvoke as a TaskRunHelper, ScheduleAndStart as a TaskScheduleHelper
    // (ActivityComputer.cs:1265-1278). InnerInvoke is also on the runtime-async resume path,
    // between Task.ExecuteWithThreadLocal and the delegate itself.
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading.Tasks!Task.InnerInvoke", CoreLib));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading.Tasks!Task.ScheduleAndStart", CoreLib));
}

// .NET 12's runtime async replaces the compiler's state machine with runtime-managed
// continuations, so AsyncStateMachineBox and AsyncMethodBuilderCore never match for a
// runtime-async method and these take their place. All are [StackTraceHidden] upstream:
// dotnet/runtime#131963 marked them precisely because stitchers -- ours included -- identify
// them by name, and nothing had guarded that contract. Listing them costs nothing on today's
// runtimes, where the frames never occur; the point is that the cleanup does not silently do
// less once runtime async ships.
TEST(AsyncFramesTest, TheRuntimeAsyncMachineryIsRuntimePlumbing)
{
    // The flat resume loop: it pops one continuation, resumes it and loops, so only ever one
    // async frame sits on the physical stack.
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!RuntimeAsyncTask<System.Int32>"
                                    ".DispatchContinuations",
                                    CoreLib));

    // The instrumented clones of the classic-async dispatch path, which appear even for
    // state-machine async once the runtime's own async profiler is enabled.
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!AsyncStateMachineDispatcher.MoveNext", CoreLib));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!AsyncTaskMethodBuilder"
                                    ".AsyncStateMachineBox<System.Int32>.InstrumentedMoveNext",
                                    CoreLib));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!AsyncTaskMethodBuilder"
                                    ".AsyncProfilerAsyncStateMachineBox<System.Int32>.MoveNextAsDispatcher",
                                    CoreLib));
}

TEST(AsyncFramesTest, EveryContinuationWrapperIndexIsRuntimePlumbing)
{
    // The runtime rotates through 32 identical [NoInlining] wrappers so an OS profiler sees
    // distinguishable return addresses, so every index has to match, not just the first.
    // These need no marker of their own -- their declaring type is
    // AsyncProfiler.ContinuationWrapper, which the pre-existing ContinuationWrapper marker
    // already matches. This test pins that, so removing that marker cannot quietly
    // un-classify all 32 of them.
    for (int index : {0, 1, 7, 31})
    {
        auto const frame = "System.Runtime.CompilerServices!AsyncProfiler.ContinuationWrapper"
                           ".Continuation_Wrapper_" +
            std::to_string(index);
        EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify(frame, CoreLib)) << "wrapper index " << index;
    }
}

TEST(AsyncFramesTest, AStateMachineBodyIsKeptAndRecognised)
{
    EXPECT_EQ(AsyncFrameKind::StateMachineMoveNext,
              AsyncFrames::Classify("Microsoft.AspNetCore.ResponseCompression!ResponseCompressionMiddleware.<InvokeCore>d__4.MoveNext",
                                    "Microsoft.AspNetCore.ResponseCompression"));
}

TEST(AsyncFramesTest, OrdinaryFramesAreUserCode)
{
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("Microsoft.AspNetCore.Session!SessionMiddleware.Invoke", "Microsoft.AspNetCore.Session"));
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("MyApp.Controllers!FolderController.CreateFolderAsync", "MyApp"));
    EXPECT_EQ(AsyncFrameKind::UserCode, AsyncFrames::Classify("System.Threading!Thread.StartCallback", CoreLib));
}

TEST(AsyncFramesTest, BlockingOnATaskIsKeptBecauseItIsARealFinding)
{
    // Sync-over-async is something you want to see in a profile, not machinery to hide.
    EXPECT_EQ(AsyncFrameKind::UserCode, AsyncFrames::Classify("System.Threading.Tasks!Task.Wait", CoreLib));
    EXPECT_EQ(AsyncFrameKind::UserCode, AsyncFrames::Classify("System.Threading.Tasks!Task.SpinThenBlockingWait", CoreLib));
    EXPECT_EQ(AsyncFrameKind::UserCode, AsyncFrames::Classify("System.Threading!ManualResetEventSlim.Wait", CoreLib));
}

// The markers are substring matches, so without the assembly gate an application type named
// "TaskContinuationHelper" would be dropped from every profile -- and so would a custom
// TaskScheduler, whose TryExecuteTaskInline and TryRunInline the user writes themselves.
// A dropped frame is invisible, which makes this worse than keeping a plumbing frame.
// PerfView guards the same way, flagging plumbing only inside mscorlib/System.Private.CoreLib.
TEST(AsyncFramesTest, ApplicationCodeIsNeverPlumbingHoweverItIsNamed)
{
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("MyApp.Scheduling!TaskContinuationHelper.Run", "MyApp"));
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("MyApp.Scheduling!BoundedScheduler.TryExecuteTaskInline", "MyApp"));
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("MyApp.Scheduling!BoundedScheduler.TryRunInline", "MyApp"));
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("MyApp.Async!ContinuationWrapper.Invoke", "MyApp"));
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("MyApp.Await!TaskAwaiterExtensions.GetResult", "MyApp"));
}

TEST(AsyncFramesTest, TheSameNameInTheRuntimeAssemblyIsStillPlumbing)
{
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Threading!ThreadPoolWorkQueue.Dispatch", CoreLib));
    // .NET Framework spells the runtime assembly differently.
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Threading!ThreadPoolWorkQueue.Dispatch", "mscorlib"));
}

TEST(AsyncFramesTest, StateMachineDetectionIsNotScopedToTheRuntimeAssembly)
{
    // A state machine is declared by the assembly that wrote the async method, never by the
    // runtime, so gating the parse on the assembly would disable the kickoff fold for every
    // async method a user writes -- which is the whole point of it.
    EXPECT_EQ(AsyncFrameKind::StateMachineMoveNext,
              AsyncFrames::Classify("MyApp.Controllers!OrdersController.<Get>d__4.MoveNext", "MyApp"));
    EXPECT_EQ(AsyncFrameKind::StateMachineMoveNext,
              AsyncFrames::Classify("Microsoft.AspNetCore.Session!SessionMiddleware.<Invoke>d__8.MoveNext",
                                    "Microsoft.AspNetCore.Session"));
}

TEST(AsyncFramesTest, AnUnresolvedAssemblyIsTreatedAsApplicationCode)
{
    // FrameStore reports an empty assembly when it could not resolve one. Keep the frame:
    // losing it is worse, because a dropped frame leaves no trace of having been dropped.
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Threading!ThreadPoolWorkQueue.Dispatch", ""));
}

TEST(AsyncFramesTest, TheStateMachineDecorationIsStrippedFromTheName)
{
    // "...<InvokeCore>d__4.MoveNext" and the kickoff "...InvokeCore" are the same method,
    // so they must end up with the same name to merge into one flamegraph node.
    EXPECT_EQ("Microsoft.AspNetCore.ResponseCompression!ResponseCompressionMiddleware.InvokeCore",
              AsyncFrames::CanonicalName("Microsoft.AspNetCore.ResponseCompression!ResponseCompressionMiddleware.<InvokeCore>d__4.MoveNext"));
}

TEST(AsyncFramesTest, AGenericStateMachineKeepsItsClassGenericArguments)
{
    EXPECT_EQ("Microsoft.AspNetCore.Server.Kestrel.Core.Internal.Http!HttpProtocol.ProcessRequests",
              AsyncFrames::CanonicalName("Microsoft.AspNetCore.Server.Kestrel.Core.Internal.Http"
                                         "!HttpProtocol.<ProcessRequests>d__237<System.__Canon>.MoveNext"));
}

TEST(AsyncFramesTest, AFrameWithNothingToRewriteCanonicalisesToItself)
{
    EXPECT_EQ("Microsoft.AspNetCore.Session!SessionMiddleware.Invoke",
              AsyncFrames::CanonicalName("Microsoft.AspNetCore.Session!SessionMiddleware.Invoke"));
}

TEST(AsyncFramesTest, AnIteratorMoveNextIsNotAStateMachineBody)
{
    // Plenty of real frames end in ".MoveNext" without being an async state machine.
    // Only the compiler's "<Method>d__N" types are, so matching on ".MoveNext" alone
    // would rename and merge unrelated user code.
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("Microsoft.EntityFrameworkCore.Query.Internal"
                                    "!SingleQueryingEnumerable.Enumerator<System.Boolean>.MoveNext",
                                    "Microsoft.EntityFrameworkCore.Relational"));
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Linq!Enumerable.ArrayWhereIterator<System.__Canon>.MoveNext", "System.Linq"));
}

TEST(AsyncFramesTest, CompletingATaskCompletionSourceIsNotTreatedAsPlumbing)
{
    // Deliberate boundary: user code calls TrySetResult itself, and dropping these
    // frames buys under 1% of the node reduction. Keeping them costs nothing.
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Threading.Tasks!TaskCompletionSource<System.Boolean>.TrySetResult", CoreLib));
}
