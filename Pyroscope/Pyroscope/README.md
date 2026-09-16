# Pyroscope .NET

This package enables applications to use Pyroscope for continuous profiling.

See [Link tracing and profiling with Span Profiles](https://grafana.com/docs/pyroscope/latest/configure-client/trace-span-profiles/) for more information.

## Prerequisites

- Your .NET application is instrumented with [Pyroscope's profiler](https://grafana.com/docs/pyroscope/latest/configure-client/language-sdks/dotnet/)

## Async stack stitching

The profiler samples physical call stacks, and `await` hides the caller: once the awaited
operation completes, the continuation resumes on a thread-pool thread rooted at
`ThreadPoolWorkQueue.Dispatch`, with the method that awaited nowhere on the stack. So for an
async-heavy service the work an endpoint caused does not nest under the endpoint, and its
inclusive time counts only the part before the first `await`.

Stitching is opt-in. Set `PYROSCOPE_ASYNC_STITCHING_ENABLED=true` to turn it on; while it is
off, pushing a scope costs nothing and the profiler reports physical stacks only.

With it on, wrap the work in a scope to get the relationship back:

```csharp
app.MapGet("/folders/{id}", async (int id, FolderService folders) =>
{
    using (Pyroscope.AsyncScope.Push("GET /folders/{id}"))
    {
        return await folders.LoadAsync(id);
    }
});
```

Every sample taken inside the scope — on whatever thread, before or after any `await` — gets
`GET /folders/{id}` as a frame above its physical stack. Scopes nest, so annotating inner
boundaries adds depth to the reconstructed tree.

Prefer names with bounded cardinality (a route template, not a URL with ids in it): each
distinct name is retained for the process lifetime, and past an internal cap new names stop
being recorded.

If your application uses OpenTelemetry, add the `Pyroscope.OpenTelemetry` package instead
and you get this for free — it opens a scope per span, named after the span.

## Labels across `await`

Dynamic labels are attached per thread, so by default labels set by a request stop at its first
`await`: the thread-pool thread running the continuation was never told about them, while the
thread that set them goes back to the pool still carrying them and mislabels whatever it picks
up next.

Set `PYROSCOPE_ASYNC_CONTEXT_PROPAGATION_ENABLED=true` and labels follow the work instead, on the
same `ExecutionContext` that carries them across an `await`, and are taken off a thread when the
flow leaves it.

Use the `Task`-returning overloads for async work:

```csharp
await Pyroscope.LabelsWrapper.Do(labels, async () =>
{
    return await folders.LoadAsync(id);
});
```

or a scope, when a callback does not fit:

```csharp
using (Pyroscope.LabelsWrapper.Push(labels))
{
    await folders.LoadAsync(id);
}
```

An `async` lambda binds to the `Task`-returning overload, so `Do` no longer silently becomes
`async void` — previously that ended the label scope at the first `await` and swallowed exceptions.
That holds whether or not propagation is enabled.

With propagation on, span context set through `Profiler.Instance.SetSpanContext` follows the async
flow the same way, so span profiles cover a whole request rather than its synchronous prologue.
