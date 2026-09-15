// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2025 Datadog, Inc.

#include "gtest/gtest.h"

#include <string>

#include "AsyncFrames.h"

// Frame names come out of FrameStore::FormatFrame as "Namespace!Type.Method", with
// generic arguments spelled inline. The samples below are real frame names taken from
// a profiled ASP.NET Core service.

TEST(AsyncFramesTest, TheContinuationBoxIsRuntimePlumbing)
{
    // One generic instantiation per async method, so these never merge in a flamegraph.
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!AsyncTaskMethodBuilder"
                                    ".AsyncStateMachineBox<System.Threading.Tasks.VoidTaskResult, "
                                    "Microsoft.AspNetCore.Session!SessionMiddleware.<Invoke>d__8>.MoveNext"));
}

TEST(AsyncFramesTest, TheContinuationDeliveryPathIsRuntimePlumbing)
{
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading!ThreadPoolWorkQueue.Dispatch"));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading!ExecutionContext.RunFromThreadPoolDispatchLoop"));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading!ExecutionContext.RunInternal"));
}

TEST(AsyncFramesTest, ThreadPoolBookkeepingIsKeptBecauseItIsRealWork)
{
    // These are not on the path from a thread root to a resumed continuation -- they are the
    // pool managing itself, and they burn measurable CPU when it is churning. Thread-pool
    // starvation is exactly the kind of thing you go to a profiler to find, so the worker
    // thread root and the queue mechanics stay.
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Threading!PortableThreadPool.WorkerThread.WorkerThreadStart"));
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Threading!PortableThreadPool.WorkerThread.MaybeAddWorkingWorker"));
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Threading!PortableThreadPool.GateThread.GateThreadStart"));
    EXPECT_EQ(AsyncFrameKind::UserCode, AsyncFrames::Classify("System.Threading!ThreadPoolWorkQueue.Enqueue"));
    EXPECT_EQ(AsyncFrameKind::UserCode, AsyncFrames::Classify("System.Threading!ThreadPoolWorkQueue.Dequeue"));
}

TEST(AsyncFramesTest, TheBuilderStartFramesAreRuntimePlumbing)
{
    // These appear only when the JIT did not inline them away, which is why the same
    // logical path lands in two different flamegraph nodes.
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!AsyncTaskMethodBuilder"
                                    ".Start<Microsoft.AspNetCore.Diagnostics!DeveloperExceptionPageMiddlewareImpl.<Invoke>d__14>"));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!AsyncMethodBuilderCore"
                                    ".Start<Microsoft.AspNetCore.Diagnostics!DeveloperExceptionPageMiddlewareImpl.<Invoke>d__14>"));
}

TEST(AsyncFramesTest, TheGenericBuildersAreRuntimePlumbingToo)
{
    // A builder for a Task<T>-returning method is itself generic, so its generic arguments
    // sit between the type name and the method: "AsyncTaskMethodBuilder<T>.Start<...>".
    // Matching on "AsyncTaskMethodBuilder." misses every one of them.
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!AsyncTaskMethodBuilder<System.__Canon>"
                                    ".Start<Pyroscope!LabelsWrapper.<Do>d__3>"));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!AsyncTaskMethodBuilder<System.Threading.Tasks.VoidTaskResult>"
                                    ".AwaitUnsafeOnCompleted<System.Runtime.CompilerServices!ValueTaskAwaiter, "
                                    "Microsoft.AspNetCore.Server.Kestrel.Transport.Sockets.Internal!SocketConnection.<DoSend>d__28>"));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!TaskAwaiter<System.__Canon>.GetResult"));

    // The pooled builder ValueTask-returning methods use is a distinct type name, and so is
    // the one `async void` uses.
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!PoolingAsyncValueTaskMethodBuilder<System.Int32>"
                                    ".Start<System.Net.Security!SslStream.<ReadAsyncInternal>d__9>"));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!AsyncVoidMethodBuilder"
                                    ".Start<Microsoft.Data.SqlClient.ManagedSni!SniPacket.<WriteToStreamAsync>d__31>"));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing,
              AsyncFrames::Classify("System.Runtime.CompilerServices!PoolingAsyncValueTaskMethodBuilder<System.Int32>"
                                    ".GetStateMachineBox<System.Net.Security!SslStream.<EnsureFullTlsFrameAsync>d__1>"));
}

TEST(AsyncFramesTest, RuntimeHelpersAreKeptBecauseTheyAreRealWork)
{
    // Also in System.Runtime.CompilerServices, and nothing to do with async: string
    // interpolation and the JIT's static/generic lookup helpers do measurable work.
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Runtime.CompilerServices!DefaultInterpolatedStringHandler.AppendLiteral"));
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Runtime.CompilerServices!StaticsHelpers.GetGCStaticBase"));
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Runtime.CompilerServices!VirtualDispatchHelpers.VirtualFunctionPointer"));
}

TEST(AsyncFramesTest, TheInlineCompletionUnwindIsRuntimePlumbing)
{
    // The recursion that fills the 1024-frame callstack budget on its own.
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading.Tasks!Task.RunContinuations"));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading.Tasks!UnwrapPromise<System.Boolean>.TrySetFromTask"));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading.Tasks!TaskContinuation.InlineIfPossibleOrElseQueue"));
    EXPECT_EQ(AsyncFrameKind::RuntimePlumbing, AsyncFrames::Classify("System.Threading.Tasks!ThreadPoolTaskScheduler.TryExecuteTaskInline"));
}

TEST(AsyncFramesTest, AStateMachineBodyIsKeptAndRecognised)
{
    EXPECT_EQ(AsyncFrameKind::StateMachineMoveNext,
              AsyncFrames::Classify("Microsoft.AspNetCore.ResponseCompression!ResponseCompressionMiddleware.<InvokeCore>d__4.MoveNext"));
}

TEST(AsyncFramesTest, OrdinaryFramesAreUserCode)
{
    EXPECT_EQ(AsyncFrameKind::UserCode, AsyncFrames::Classify("Microsoft.AspNetCore.Session!SessionMiddleware.Invoke"));
    EXPECT_EQ(AsyncFrameKind::UserCode, AsyncFrames::Classify("MyApp.Controllers!FolderController.CreateFolderAsync"));
    EXPECT_EQ(AsyncFrameKind::UserCode, AsyncFrames::Classify("System.Threading!Thread.StartCallback"));
}

TEST(AsyncFramesTest, BlockingOnATaskIsKeptBecauseItIsARealFinding)
{
    // Sync-over-async is something you want to see in a profile, not machinery to hide.
    EXPECT_EQ(AsyncFrameKind::UserCode, AsyncFrames::Classify("System.Threading.Tasks!Task.Wait"));
    EXPECT_EQ(AsyncFrameKind::UserCode, AsyncFrames::Classify("System.Threading.Tasks!Task.SpinThenBlockingWait"));
    EXPECT_EQ(AsyncFrameKind::UserCode, AsyncFrames::Classify("System.Threading!ManualResetEventSlim.Wait"));
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
                                    "!SingleQueryingEnumerable.Enumerator<System.Boolean>.MoveNext"));
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Linq!Enumerable.ArrayWhereIterator<System.__Canon>.MoveNext"));
}

TEST(AsyncFramesTest, CompletingATaskCompletionSourceIsNotTreatedAsPlumbing)
{
    // Deliberate boundary: user code calls TrySetResult itself, and dropping these
    // frames buys under 1% of the node reduction. Keeping them costs nothing.
    EXPECT_EQ(AsyncFrameKind::UserCode,
              AsyncFrames::Classify("System.Threading.Tasks!TaskCompletionSource<System.Boolean>.TrySetResult"));
}
