# Async context propagation

The profiler's per-sample context — the logical call tree, the dynamic labels, the active
span — is **per OS thread**. `async`/`await` moves work between threads, so for an async-heavy
service all three break in the same way.

This describes the three things done about that. All three are behind one switch,
`PYROSCOPE_ASYNC_PROFILING_ENABLED`, which is **off unless set**, so a deployment keeps the
profiles it has until it opts in. They are one switch rather than three because there is no
steady-state reason to want a subset: each is inert unless the application uses the
corresponding API, and none costs anything while inert.

## The problem

The C# compiler turns each `async` method into a state machine driven by `MoveNext`. When a
method awaits something incomplete, `MoveNext` schedules a continuation and **returns**,
unwinding the caller's stack. Later the continuation runs, usually on a thread-pool thread,
and its physical stack is:

```
[thread start] -> PortableThreadPool.WorkerThread -> ThreadPoolWorkQueue.Dispatch
   -> ExecutionContext.RunInternal -> AsyncStateMachineBox`1.MoveNext
      -> <SomeMethodAsync>d__N.MoveNext -> ...the actual work...
```

The method that awaited — the controller action, the request handler, the background job — is
nowhere on that stack, and the thread was never told about the request's labels or span.
Consequences:

- inclusive time under any logical entry point counts only the work before its first `await`;
- the same action appears at many unrelated stack depths, each a small slice;
- flamegraphs read as a flat sea of `AsyncStateMachineBox.MoveNext` roots;
- `group_by` on a dynamic label misses everything after the first `await`;
- span profiles cover only a request's synchronous prologue;
- worse than losing labels: the thread that set them goes back to the pool **still carrying
  them**, so whatever it picks up next is attributed to the wrong request.

## The one thing that survives an await

While the physical stack is lost, the `ExecutionContext` is **captured at the await and
restored on the continuation**. That is why `AsyncLocal<T>` values survive awaits.

`ProfilingContext` (managed) rides that channel. It keeps **one** `AsyncLocal<T>`, holding an
immutable snapshot of all three pieces of context, **with a change handler**. The CLR invokes
that handler on the resuming thread every time it restores an `ExecutionContext` — precisely
when a continuation is about to run — and again when the thread pool resets the thread
afterwards. The handler publishes the whole context into the sampled thread's slots; the reset
half is what stops unrelated work from inheriting it.

One local rather than one per piece of context, for two reasons. It is 2.6x cheaper (see
below): on every context switch the CLR walks the notifying-local array of both the outgoing
and the incoming context, probes both value maps for each entry, and invokes the handler only
for those whose value actually changed. Three locals therefore pay three walks, six map probes
and — for a flow that set all three — three callbacks, where one local pays one, two and one.
And an immutable snapshot makes closing scopes order-independent: each `Dispose` puts back its
own field and leaves the others alone, so ending a span inside an open label scope cannot drop
the labels — which a "restore the whole previous value" design would.

A per-thread cache of what was last published keeps the sink call itself off most switches,
and mutations (`PushScope`, `PushLabels`, `PushSpan`) publish explicitly rather than relying on
the AsyncLocal setter's notification, which the runtime skips when a value is unchanged.

### Logical call tree (async stack stitching)

`AsyncScope.Push("GET /folders/{id}")` — or, automatically, one scope per span from
`Pyroscope.OpenTelemetry.PyroscopeSpanProcessor` — names a logical scope.
`AsyncScopeStore` interns chains by `(parent, name)`, so it holds one node per distinct scope
*path* rather than one per request: bounded by the application's code paths, like the node
count of a flamegraph. The sampler copies the chain's id into the sample while the thread is
suspended or in its signal handler (an id only; resolving it needs a lock we must not take
there), and `RawSampleTransformer::SetStack` appends the chain's frames after the physical
ones. The callstack is leaf-first, so appending puts them *above* the physical root,
re-rooting the continuation under the scope that caused it.

### Dynamic labels and span context

Labels are stored per thread in `ManagedThreadInfo::GetTags()`, and the per-key route is
expensive on a hot path: `Tags::Set` takes a process-wide mutex to resolve the key id, and
interning the value string takes another. Doing that several times per `await` would
serialize every continuation in the application on those two locks.

So `DynamicTagSetStore` interns whole sets by content, and applying one afterwards is a plain
`Tags` assignment — 16 `AsyncRefCountedString` copies, each an atomic refcount increment.
Releases stay lock-free too, because the interned set keeps a permanent reference to every
string it holds, so no refcount reaches zero. The assignment is also friendlier to the
sampler than the per-key route: it never leaves the thread's tags momentarily empty the way
`ClearAll()` followed by N `Set()` calls does.

Span context needs no interning — it is three integers written into the per-thread block the
profiler already hands out, via `ContextTracker`.

Leaving the switch off does not turn labels off: they are applied on the thread that set them,
which is what the profiler did before.

### Async frame cleanup

Stitching gives a continuation the right parent, but the continuation's own stack is still
the physical one, and in an async-heavy service **about a third of its frames are the
machinery** — the continuation box, thread-pool dispatch, `ExecutionContext.Run`, the
builder's `Start`, the inline-completion unwind. Measured on a real service: 27.9% of the
frames in a typical wall stack, 34.3% for CPU, holding only 1.7% and 1.9% of self time.

Frame count is not the real damage. These frames appear **inconsistently**, because whether
one survives depends on what the JIT inlined for that instantiation, so two samples on the
same logical path land in different flamegraph nodes and their times never add up. On that
service `ResponseCompressionMiddleware.InvokeCore` was spread over 25 nodes; the two largest
had ancestor chains differing by exactly one `AsyncMethodBuilderCore.Start<…>` frame. The
box and `Start<…>` frames are also generic over the state machine type, so there is one
distinct frame *name* per async method: 672 `AsyncStateMachineBox` names out of 6,048.

So `AsyncFrames::Classify` labels each frame (`AsyncFrameKind`) and
`RawSampleTransformer::SetStack` applies two rules:

- **drop the machinery.** No synthetic "[async]" marker replaces it: a marker is itself a
  node whose presence varies per sample, which measurably *re-splits* the tree (17,622 nodes
  versus 15,495 without one). The dropped self time falls to the caller that ran the
  machinery, which is the code whose `await` it was servicing.
- **fold an async method's kickoff frame into its state machine body.** The compiler emits
  both `Class.Method` and `Class.<Method>d__4.MoveNext`, one directly above the other;
  canonicalising the body's name to `Class.Method` and dropping the kickoff above it puts a
  sample that caught only the kickoff on the same node as one that caught the body. Only
  that pair collapses, so genuine async recursion keeps its depth.

Both are computed **once per method**, as the frame store first resolves and caches it, so
neither costs anything per sample.

On the same service this removes 41% of wall tree nodes and 35% of CPU ones with self time
preserved exactly, collapses that middleware from 25 nodes to 5 (wall) and 32 to 7 (CPU), and
takes the maximum stack depth from 1000 to 94 — the 1000 was a `Task.RunContinuations` →
`UnwrapPromise.Invoke` → `TrySetFromTask` cycle repeated 333 times, which fills the whole
1024-frame `Callstack` budget and so costs the *outermost* frames, the stitched scope frame
among them.

Two things to know if you extend the marker list. A builder for a `Task<T>`-returning method is
generic, so its frame reads `AsyncTaskMethodBuilder<T>.Start<…>` — the generic arguments sit
between the type name and the method. Markers therefore match the type name without a
trailing `.`; requiring one silently missed every `Task<T>` builder, `TaskAwaiter<T>` and the
pooled `ValueTask` builder, which is what `async_frame_cleanup_test.go` caught and the unit
tests did not.

And the markers only apply inside the runtime's own assembly — `System.Private.CoreLib`, or
`mscorlib` on .NET Framework. They are substring matches, so unqualified they would drop
application code: a type named `TaskContinuationHelper` matches `TaskContinuation`, and
`TryExecuteTaskInline` and `TryRunInline` are methods a user writes when implementing a custom
`TaskScheduler`. A frame whose assembly could not be resolved is treated as application code,
because a dropped frame leaves no trace of having been dropped. The state machine parse is
deliberately *not* gated: a state machine belongs to the assembly that declared the async
method, so gating it would disable the kickoff fold for every async method a user writes.

What it deliberately does not do:

- **the blocking waits stay.** `Task.Wait`, `Task.SpinThenBlockingWait`,
  `ManualResetEventSlim.Wait`: sync-over-async is a finding, not noise.
- **the thread pool's own bookkeeping stays.** Only `ThreadPoolWorkQueue.Dispatch` goes, not
  the whole type: `PortableThreadPool.WorkerThread.*`, `GateThread` and the work-stealing
  queue are real work — `MaybeAddWorkingWorker` alone held 4.8s of self time on the service
  measured, which is what pool churn looks like. Dropping them saves a few dozen tree nodes
  out of ten thousand and hides thread-pool starvation, so they stay. The worker thread root
  is also on every pool stack, so keeping it costs nothing in merging.
- **`TaskCompletionSource.TrySetResult` stays.** Catching the generic `Task<T>` forms needs a
  loose `.TrySetResult` match that also swallows the calls user code makes itself, and it is
  worth under 1% of the frames removed.
- **a frame the JIT inlined away is not recovered.** When inlining dropped a real frame from
  one sample and not another, the two paths still differ; the profiler cannot put back a
  frame that was never on the stack.

Note for anyone comparing against an older baseline: a gate that measures the share of time
under `AsyncStateMachineBox` roots reads ~0% with this on, because those frames no longer
exist. That is not the stitching metric improving — measure the share under a scope frame
instead.

### Degradation

Both stores cap their size (8192 scopes, 16384 label sets) and warn once when they fill,
because filling them means the application is using unbounded names or label values — and
therefore producing that many series. Past the cap:

- a scope keeps its parent's id, so the work stays attributed to the enclosing scope;
- a label set reports `NoDynamicTagSetEver`, and the managed side applies it the old way, on
  the thread that set it. No worse than before propagation existed, and no per-switch cost.

`Tags` also allows only 16 distinct label **keys** per process; a set using a key past that
limit is rejected whole rather than interned partially, and warns once.

Covered profile types: wall, CPU (both the `StackSamplerLoop` and the Linux `timer_create`
sampler), allocation, exception and contention — every place that copies a thread's dynamic
tags onto a sample now copies its scope id too.

## What it costs

Measured on .NET 9, 10 cores, against a 3-deep async chain whose innermost `await` goes
async — which costs **4.3 `ExecutionContext` switches per await**:

| | ns per switch | added | per await |
| --- | --- | --- | --- |
| `ExecutionContext.Run`, no notifying `AsyncLocal` | 16 | — | — |
| **one notifying local, deduped (what ships)** | 108 | +92 | **+400 ns** |
| a local per piece of context, deduped | 258 | +242 | +1,049 ns |
| a local per piece of context, no dedupe | 399 | +384 | +1,661 ns |

The last two rows are why the three pieces of context share one `AsyncLocal` and one
per-thread cache. On each switch the CLR walks the notifying locals of both the outgoing and
the incoming `ExecutionContext`, probing both value maps per entry, and calls back only where
the value changed by reference (`ExecutionContext.OnValuesChanged`); a local each therefore
multiplies the walk, the probes and — for a flow that sets all three — the callbacks. The
native library measured here is a Debug `-O0` build, so the P/Invoke is pessimistic. Only
flows that actually open a scope or set labels pay any of this: an `ExecutionContext` with no
notifying `AsyncLocal` in it costs the runtime nothing at a switch.

Two properties of that channel are load-bearing rather than incidental. A handler that throws
reaches `Environment.FailFast`, which is why `Publish` catches everything and latches instead
of letting anything escape. And notifying locals are a process-global tax in both directions:
any other component that registers one makes every switch walk its array too, and ours does
the same to everyone else.

For scale, the TPL-event approach below costs +1,560 ns per await at minimum and +10,016 ns
with the keywords that make it usable — and those figures exclude stack capture. At 400 ns
per await, an async-heavy service doing 50 awaits per request at 1,000 req/s spends about 2%
of one core on propagation.

The hot path is deliberately narrow. `SetCurrentProfilingContext` reads the calling thread's
own `ManagedThreadInfo` rather than going through `ManagedThreadList`, whose lookup takes a
process-wide lock that would serialize every continuation. Interning is off the hot path
entirely: ids are cached per `LabelSet` instance and per distinct content, so a request that
repeats a code path makes no interning call at all. Side-table sizes are exported as the
`dotnet_memory_footprint_async_scope_store` and
`dotnet_memory_footprint_dynamic_tag_set_store` metrics.

## Why not reconstruct the real per-await stacks

Three approaches recover the *physical* chain (`A -> B -> C`) that led to each await. None is
viable in this repository; the measurements are from
`System.Threading.Tasks.TplEventSource` on .NET 9, 10 cores, 200k awaits.

**TPL runtime events (PerfView's approach).** `AwaitTaskContinuationScheduled` fires on the
awaiting thread and EventPipe can attach the emitting thread's stack, which is exactly the
scheduling stack we would want to prepend. But nothing in that keyword set says *which*
continuation a given thread is running, so a sample cannot be joined to a scheduling stack.
The events that do bracket a continuation's execution on a thread —
`TraceSynchronousWorkBegin`/`End`, under the `AsyncCausality*` keywords — cost far too much:

| keywords | events per await | added CPU per await | CPU overhead |
| --- | --- | --- | --- |
| `TaskTransfer\|Tasks\|TaskStops\|TasksFlowActivityIds` | 5.0 | 1.6 us | +26% |
| the above plus `AsyncCausalityOperation\|Relation\|SynchronousWork` | 22.8 | 10.0 us | +157% |

That is measured with an in-process `EventListener` that does **not** capture stacks; the real
thing, capturing a stack per await, is strictly worse. The profiler already refuses to start
below a 1-core CPU limit, so 2.5x the application's CPU is not a trade we can offer.
(`TasksFlowActivityIds` also did not produce usable activity ids in this configuration, so
the existing `ActivityHelpers` correlation used for HTTP events does not help either.)

**Capture-at-schedule with a side table keyed by the state machine box.** Needs IL rewriting
of `AwaitUnsafeOnCompleted`, and the fork deleted the tracer, so there is no IL rewriting
infrastructure here. It also needs the box's identity at sample time, which an
instruction-pointer stack walk does not give.

**Walking `Task.m_continuationObject`.** At *sample* time this is unsafe: it reads managed
object fields from a `SIGUSR1` handler on Linux (`LinuxStackFramesCollector`), while the GC may
be moving those objects, using field layouts that are runtime-internal.

At *checkpoint* time it is safe, and it is what the platform itself now does — .NET 12's
`AsyncProfiler` walks the continuation chain from managed code inside `InstrumentedMoveNext`,
emitting one frame per logical caller. So the approach is not inherently unsafe; it is simply
unavailable here. It needs a hook at the `await`, which means IL rewriting the fork no longer
has, and the managed entry points (`IAsyncStateMachineBox.GetDiagnosticData`,
`AsyncInstrumentation`) are `internal` and .NET 12+.

Scopes give up the intermediate `A -> B -> C` frames — the thread-pool dispatch frames sit
between the scope and the work instead — and in exchange they are safe, cost one integer
P/Invoke per continuation, and deliver what the fragmentation actually broke: inclusive time
per entry point, a stable rooted depth, and a per-endpoint drill-down. Fidelity scales with
annotation density, and OpenTelemetry instrumentation supplies it for free at span
granularity.

## How this compares to the rest of the ecosystem

Researched against `dotnet/dotnet` `main` (.NET 12 alpha, per `src/runtime/eng/Versions.props`)
and `microsoft/perfview` `main`.

**The platform gives an ICorProfiler nothing.** `corprof.idl` has no async awareness — its only
"async" tokens are the dead .NET Remoting callbacks. `DoStackSnapshot` walks the physical stack
and that is the whole contract. .NET 12's logical async stack is exposed to EventPipe, to
exception traces and to the debugger, and explicitly not to a profiler: the only runtime-async
change on the profiling side is a guard returning `CORPROF_E_DATAINCOMPLETE` for continuation
types, whose `MethodTable`s have no metadata. `dotnet/runtime#14434`, asking for an API to
reason about async call stacks, has been open since 2015.

**The runtime's own sampler has the problem this document fixes.** The EventPipe sample
profiler is stop-the-world, IP-only and physical-stack, with a 100-frame cap that truncates the
*outermost* frames silently. Its `ThreadSample` events carry an all-zero ActivityID, because
`ep_write_sample_profile_event` bypasses the path that would fill it — so the one hook that
could have given its samples a logical async correlation is unused.

**Name matching is what the reference implementation does too.** PerfView identifies async
plumbing with hardcoded name matches — `ExecutionContext.Run`, `Task.Execute`, `.InnerInvoke`,
`AsyncTaskMethodBuilder…AwaitUnsafeOnCompleted` (`ActivityComputer.cs:1226-1286`) — and it
*requires symbols* to do it, carrying two `// TODO FIX NOW fix if you don't have symbols`
comments at its classification sites. It also scopes matching to
mscorlib/System.Private.CoreLib, which is why `AsyncFrames::Classify` takes the declaring
assembly. Upstream has since hardened these frame names deliberately
(`dotnet/runtime#131963`) precisely because stitchers depend on them.

**The TPL route is worse than we measured, and has a coverage hole.** Our figures were +26% CPU
at the minimum useful keyword set and +157% with the causality keywords. Microsoft's own numbers
for the same mechanism are >75% throughput loss at 1M async transitions/sec and ~45% at 100K/sec
(`dotnet/runtime#127238`, the PR replacing it). Separately, `IValueTaskSource`-backed `ValueTask`
awaits emit **no** TPL events, so pooled/Pipelines/Channels-heavy code is invisible to that
approach entirely. Note also that PerfView's TPL collection has been opt-in since 2021, so a
default PerfView or `dotnet-trace` capture yields no stitching at all even though the
`ActivityComputer` machinery is present and enabled.

**Do not drive the drop list from `[StackTraceHidden]`.** It is calibrated for exception traces,
where the compiler's `try/catch` inside `MoveNext` stops capture before
`ExecutionContext.RunInternal`, `Task.ExecuteEntry` and `ThreadPoolWorkQueue.Dispatch` ever
appear — so it omits most of what a sampler sees, and `AsyncMethodBuilderCore` carries no such
attribute. It also misses frames hidden by the separate `AggressiveInlining` predicate
(`StackTrace.cs:379-386`), such as `ValueTaskAwaiter.GetResult` and
`AsyncTaskMethodBuilder<T>.Start`. .NET solves the two halves of this with two independent
mechanisms — `[StackTraceHidden]` for the noise, `TryResolveStateMachineMethod` for the mangled
names — which is the same decomposition as the two rules above.

We also do not need the `MethodImplAttributes.Async` short-circuit upstream uses
(`StackTrace.cs:240`). That guard exists because its state-machine detection is *type*-based
(`[CompilerGenerated]` plus `IAsyncStateMachine`), which a runtime-async method can satisfy.
Ours keys on the name shape — `.MoveNext` on a type ending `d__<digits>` — which runtime async
never produces.

**Where this sits among other profilers.** Async stack stitching exists in three families:
TPL-event causality (PerfView/TraceEvent, and therefore `dotnet-trace convert`; JetBrains
dotTrace in Timeline mode), `MoveNext` re-parenting by an instrumenting agent (Redgate ANTS, the
Visual Studio Instrumentation tool), and continuation-graph walking in break/dump mode (the
Visual Studio debugger, SOS `dumpasync`). Among *continuous* profilers the only product
documenting it is Azure Application Insights Profiler, which inherits PerfView's machinery, runs
in triggered bursts rather than continuously, and documents 5–15% CPU and memory overhead.
Dynatrace has a .NET CPU profiler with no async handling documented; Instana lists .NET Core as
CPU-usage-only while giving Node.js async call profiles; Sentry's .NET profiler has the TPL
provider commented out. What vendors normally mean by "async support" is context propagation for
tags and spans, not a reconstructed stack.

Worth treating as a target rather than a boast: **nobody ships correct inclusive time under an
async entry point.** dotTrace deliberately greys out continuation and await time and excludes it
from the parent's total; ANTS warns its wallclock totals can exceed real elapsed time; PerfView
charges creator-side but fills gaps with `UNKNOWN_ASYNC`.

**This design extends the upstream mechanism rather than departing from it.** The tracer this
fork descends from already creates `new AsyncLocal<Scope>(OnScopeChanged)`, and only when the
profiler's context tracker is enabled (`tracer/src/Datadog.Trace/AsyncLocalScopeManager.cs`
upstream). It carries the active span and nothing else. `ProfilingContext` widens that same
channel to a snapshot of scope, labels and span, and then uses it to fix stacks rather than only
tags. Upstream's profiler has no async frame handling at all.

## Known limitation: causality is a graph, not a list

`Task.WhenAll` and `WhenAny` fork and join, so a continuation can have several logical parents
while a scope chain is a list and records one. This is inherent to every stitcher rather than
specific to this implementation — the TPL mechanism has the same problem, since only the last
task to complete forms the chain. Scopes degrade predictably: the work is attributed to the
enclosing scope that was current when the continuation resumed.

## Forward risk: runtime async

- `AsyncStateMachineBox` and `AsyncMethodBuilderCore` stop matching for runtime-async methods.
  Not wrong, just inert. `DispatchContinuations`, `AsyncStateMachineDispatcher`,
  `InstrumentedMoveNext` and `MoveNextAsDispatcher` are in the marker list for this reason; the
  32 `Continuation_Wrapper_N` frames are covered by the existing `ContinuationWrapper` marker.
- The kickoff fold stops firing: a runtime-async method is a real method with a real name, so
  there is no `<Method>d__N.MoveNext` pair to collapse. Nothing to do — the fold has no work.
- **Re-measure the +400 ns per await.** `DispatchContinuations` restores the `ExecutionContext`
  per resumed continuation rather than once per work item, so the table above will not carry
  over.
- `AsyncProfilerEventSource` is worth watching as a future source of real per-await frames — the
  runtime reconstructs a whole async stack from one event at about 5 ns per resume, against the
  TPL figures above. Consuming it means an EventPipe consumer alongside the profiling API, its
  wire format is explicitly "internal and can be changed without notice", and it has no consumer
  tooling in `dotnet/diagnostics` yet.

## Verification

- `profiler/test/.../AsyncScopeStoreTest.cpp` — scope interning, frame ordering, the
  depth/capacity caps, concurrent push/read.
- `profiler/test/.../DynamicTagSetStoreTest.cpp` — set interning, that applying a set
  *replaces* rather than merges, key-limit and capacity handling, concurrent intern/apply.
- `profiler/test/.../AsyncScopeStitchingTest.cpp` — a sample's frames with and without a
  scope, and that a stale id is ignored rather than misattributed.
- `profiler/test/.../AsyncFramesTest.cpp` — the classification and the rename, on real frame
  names: the machinery, the blocking waits and `TaskCompletionSource` that must survive it,
  and the iterator `MoveNext` frames that must not be mistaken for state machine bodies.
- `profiler/test/.../AsyncFrameCleanupTest.cpp` — the two stack rules: two samples differing
  only by a machinery frame come out identical, genuine recursion is kept, a stack that is
  machinery all the way down keeps its leaf rather than being emptied, scope frames survive,
  and an inlined-away frame is not recovered.
- `Pyroscope/Pyroscope.Tests/AsyncScopePropagationTests.cs` and `ProfilingContextTests.cs` —
  the propagation itself: scope, labels and span each reach a thread-pool continuation *and* a
  thread created after the flow started, are removed when the flow leaves a thread, do not
  leak between concurrent flows, survive a missing native library, and cost one publish per
  switch rather than one per `AsyncLocal`.
- `Pyroscope/Pyroscope.Tests/NativeInteropSmokeTests.cs` — calls the real entry points, so a
  P/Invoke signature the runtime refuses to marshal fails here rather than showing up as
  silently missing labels.
- `Pyroscope/Pyroscope.OpenTelemetry.Tests/PyroscopeSpanProcessorAsyncScopeTests.cs` — one
  scope per span, nesting, and the opt-out.
- `integration-test/async_stitching_test.go` and `async_labels_test.go` — end to end against a
  real Pyroscope: every sample of the work an endpoint performs after its `await` is rooted at
  the endpoint and carries its label, a synchronous endpoint's work is neither, and with each
  switch off the corresponding attribution disappears. `startAsyncStitchingApp` enables the
  switch, and the "disabled" tests pass it as false.
- `integration-test/async_frame_cleanup_test.go` — end to end, that no machinery frame and no
  state-machine-decorated name reaches a collected profile, and that both come back with the
  switch off, which is what proves the check is not passing vacuously.
