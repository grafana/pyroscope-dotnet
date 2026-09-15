# Async context propagation

The profiler's per-sample context — the logical call tree, the dynamic labels, the active
span — used to be **per OS thread**. `async`/`await` moves work between threads, so for an
async-heavy service all three broke in the same way.

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
below), because the CLR fires a notification per notifying local on every switch. And an
immutable snapshot makes closing scopes order-independent: each `Dispose` puts back its own
field and leaves the others alone, so ending a span inside an open label scope cannot drop the
labels — which a "restore the whole previous value" design would.

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

Switch: `PYROSCOPE_ASYNC_STITCHING_ENABLED=false`.

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

Switch: `PYROSCOPE_ASYNC_CONTEXT_PROPAGATION_ENABLED=false`. Turning it off does not turn
labels off: they revert to being applied on the thread that set them, which is what the
profiler did before.

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

One thing to know if you extend the marker list: a builder for a `Task<T>`-returning method is
generic, so its frame reads `AsyncTaskMethodBuilder<T>.Start<…>` — the generic arguments sit
between the type name and the method. Markers therefore match the type name without a
trailing `.`; requiring one silently missed every `Task<T>` builder, `TaskAwaiter<T>` and the
pooled `ValueTask` builder, which is what `async_frame_cleanup_test.go` caught and the unit
tests did not.

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

Switch: `PYROSCOPE_ASYNC_FRAME_CLEANUP_ENABLED=false`.

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
per-thread cache: a local each would have cost 2.6x as much, since the CLR fires a
notification per notifying local on every switch. The native library measured here is a
Debug `-O0` build, so the P/Invoke is pessimistic. Only flows that actually open a scope or
set labels pay any of this: an `ExecutionContext` with no notifying `AsyncLocal` in it costs
the runtime nothing at a switch.

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

**Walking `Task.m_continuationObject` at sample time.** Reads managed object fields from a
`SIGUSR1` handler on Linux (`LinuxStackFramesCollector`), while the GC may be moving those
objects, using field layouts that are runtime-internal.

Scopes give up the intermediate `A -> B -> C` frames — the thread-pool dispatch frames sit
between the scope and the work instead — and in exchange they are safe, cost one integer
P/Invoke per continuation, and deliver what the fragmentation actually broke: inclusive time
per entry point, a stable rooted depth, and a per-endpoint drill-down. Fidelity scales with
annotation density, and OpenTelemetry instrumentation supplies it for free at span
granularity.

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
  switch off the corresponding attribution disappears.
- `integration-test/async_frame_cleanup_test.go` — end to end, that no machinery frame and no
  state-machine-decorated name reaches a collected profile, and that both come back with the
  switch off (which is what proves the check is not passing vacuously).
