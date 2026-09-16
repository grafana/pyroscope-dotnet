# Async Profiling Research Follow-ups — Implementation Plan

**Status: complete.** All tasks implemented on `feat/async-context-propagation`. This document
is kept as the record of what was decided and what changed during execution.

**Goal:** Apply the findings of a comparison against the .NET runtime's own async profiling
machinery, PerfView, and the commercial profiler landscape to the async work on this branch:
fix one correctness risk, close marker gaps, make stitching observable, and correct two
documentation claims that were mechanically wrong.

**Sources compared:** `dotnet/dotnet` `main` (.NET 12 alpha, per `src/runtime/eng/Versions.props`),
`microsoft/perfview` `main`, and the upstream `dd-trace-dotnet` tracer/profiler. The durable
technical conclusions live in
`profiler/src/ProfilerEngine/Datadog.Profiler.Native/docs/async-context-propagation.md`
("How this compares to the rest of the ecosystem", "Known limitation", "Forward risk").

---

## Commits

| Commit | Task |
| --- | --- |
| `576628dde` | Scope plumbing markers to the runtime assembly |
| `a3744e32d` | Drop `Task.InnerInvoke` and `Task.ScheduleAndStart` |
| `fe8a9c8ce` | Recognise the runtime-async machinery frames |
| `ed2221bde` | Export async stitch coverage metrics |
| `294e07d67` | Correct the AsyncLocal cost model and the continuation-walk rejection |

---

## Task 1 — Scope plumbing markers to the runtime assembly

The correctness fix, and the only behavioural risk in the set: it *narrows* when markers fire,
so fewer frames are dropped, never more.

`AsyncFrames::Classify` matched `PlumbingMarkers` as substrings of a bare frame name with no
module check, so application code could be dropped from a profile silently. A type named
`TaskContinuationHelper` matches `"TaskContinuation"`; `TryExecuteTaskInline` and `TryRunInline`
are methods a user writes when implementing a custom `TaskScheduler`. PerfView guards the same
way, flagging plumbing only inside mscorlib/System.Private.CoreLib (`ActivityComputer.cs:1255`).

`Classify` now takes the declaring assembly, and `IsRuntimeAssembly` accepts
`System.Private.CoreLib` and `mscorlib`. An unresolved (empty) assembly is treated as
application code, because a dropped frame leaves no trace of having been dropped.

**The load-bearing constraint:** state-machine detection is *not* gated. A state machine belongs
to the assembly that declared the async method, so gating it would disable the kickoff fold for
every async method a user writes.

`FrameStoreHelper` derives a plausible assembly from the fixture's namespace rather than
claiming everything is the runtime, so the gate is exercised rather than bypassed in tests.

## Task 2 — `Task.InnerInvoke` and `Task.ScheduleAndStart`

Absent from the marker list; PerfView removes both (`ActivityComputer.cs:1265-1278`).
`InnerInvoke` is also on the runtime-async resume path.

**Not measured.** `InnerInvoke` appears for delegate-based `Task.Run(() => …)` bodies rather than
`async Task` state machines, so its share of removed frames on a real service is unknown. The
integration suite that would answer it needs Docker and a live Pyroscope. The frame-share figures
in the design doc still describe the previous marker set.

## Task 3 — Runtime-async machinery frames

For a runtime-async method `AsyncStateMachineBox` and `AsyncMethodBuilderCore` never appear, so
those markers go inert and the cleanup quietly does less. Added `DispatchContinuations`,
`AsyncStateMachineDispatcher`, `InstrumentedMoveNext`, `MoveNextAsDispatcher` — all
`[StackTraceHidden]` upstream, and all named in `dotnet/runtime#131963`, which hardened these
exact names because stitchers identify them by name and nothing had guarded that contract.

**Changed during execution:** the 32 `Continuation_Wrapper_N` frames turned out to need no
marker. Their declaring type is `AsyncProfiler.ContinuationWrapper`, already matched by the
pre-existing `ContinuationWrapper` marker (which was there for the classic inline-completion
type). A test pins this so removing that marker cannot quietly un-classify all 32.

## Task 4 — Stitch coverage metrics

Store sizes and cap warnings were the only observability. PerfView shows the failure mode to
avoid: when it cannot find the thread-pool transition it silently returns an unstitched stack
(`ActivityComputer.cs:1004`).

Three `ProxyMetric`s: `dotnet_async_stitching_samples_total`,
`..._samples_stitched_total`, `..._stale_scope_ids_total`. The third is the one worth having — a
stale id means the scope store is too small for the application's scope count, and without a
separate counter that is indistinguishable from ordinary synchronous work, which is also
unstitched and also fine. All three stay at zero when stitching is off, so an off switch does not
read as a fault.

`AsyncScopeStore::ForEachFrame` now returns the frame count, which is what makes the distinction
cheap; callers that ignore it are unaffected.

## Task 5 — Documentation

Two corrections plus the comparison material. See the commit and the design doc.

---

## Deliberately not implemented

**`MethodImplAttributes.Async` (`miAsync = 0x2000`) short-circuit — dropped as unnecessary.**
Originally proposed as "required"; that was wrong. Upstream needs it (`StackTrace.cs:240`)
because its state-machine detection is *type*-based (`[CompilerGenerated]` plus
`IAsyncStateMachine`), which a runtime-async method can satisfy. Ours is *name-shape*-based: the
frame must end `.MoveNext` **and** the type must end `d__<digits>`. Runtime async emits real
method names with no compiler state-machine type, so it produces neither and the parse cannot
false-positive on it. The guard could never fire. The contrast is documented in the design doc
instead.

**Full metadata-driven state-machine resolution — deferred.** Reading `AsyncStateMachineAttribute`
through the `IMetaDataImport2` already live at `FrameStore.cpp:206` would make the kickoff fold
exact rather than heuristic. Ranked low because PerfView, the reference implementation, also
matches by name *and* requires symbols to do it (two `// TODO FIX NOW fix if you don't have
symbols` comments sit at its classification sites). Our metadata-resolved names are already more
robust than the canonical implementation's, so replacing a well-tested name parse with a
reflection walk over the enclosing type's methods is real regression risk for little gain.

---

## Verification performed

- Native unit tests: **644 passed, 1 skipped, 2 failed**. Both failures are
  `LoggerTest.EnsureLogFilesAreFoundAtDefaultLocation` and `LoggerTest.FixBugStaticWcharArray`,
  both asserting `fs::exists(logFilePath)` on a log file this environment cannot create. Neither
  is related to anything changed here.
- Async suites specifically (`AsyncFrames*`, `AsyncFrameCleanup*`, `AsyncScope*`,
  `DynamicTagSetStore*`, `FrameStore*`): 70 passed. `FrameStore*` included because Task 1 changed
  a `FrameStore` call site.
- `Pyroscope.Profiler.Native` builds clean.

**Not run:** the managed test suite, because `global.json` pins SDK 10.0.203 and this environment
has 9.0.110 and 10.0.112 — a pre-existing limitation unrelated to these changes. The only managed
edit is a comment; the comment-between-`try`-and-`catch` shape was verified to compile in a
scratch project. Also not run: `integration-test/`, which needs Docker and a live Pyroscope.

---

## Post-merge follow-ups

- Close `grafana/pyroscope-dotnet` issue #50 ("How to profile async code?", open and unanswered
  since Nov 2023) and abandoned PR #118 ("Add async support to LabelsWrapper"). Outward-facing
  actions on a public repo — needs explicit confirmation, and belongs after this merges.
- Re-derive the frame-share figures in the design doc against the current marker set, and measure
  `Task.InnerInvoke`'s actual contribution, once the integration suite can run.
- Re-measure the +400 ns per await once a runtime-async-capable .NET is available:
  `DispatchContinuations` restores the `ExecutionContext` per continuation rather than once per
  work item.
