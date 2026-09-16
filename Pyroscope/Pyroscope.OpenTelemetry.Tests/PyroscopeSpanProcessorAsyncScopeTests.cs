using System.Diagnostics;
using NUnit.Framework;

namespace Pyroscope.OpenTelemetry.Tests;

// Which scopes a span's start and end leave behind. The processor is driven directly rather than
// through the OpenTelemetry SDK; there is no native profiler in this process, so scope ids are all
// zero and the assertions are on the scope chain.
[TestFixture]
public class PyroscopeSpanProcessorAsyncScopeTests
{
    [Test]
    public void SpanStartOpensAScopeNamedAfterTheSpan()
    {
        var processor = new PyroscopeSpanProcessor();
        using var activity = new Activity("GET").Start();
        activity.DisplayName = "GET /folders/{id}";

        processor.OnStart(activity);
        try
        {
            // DisplayName, not OperationName: for ASP.NET Core it is the route template,
            // which is the low-cardinality name we want as a frame.
            Assert.That(AsyncScope.Current, Is.EqualTo("GET /folders/{id}"));
        }
        finally
        {
            processor.OnEnd(activity);
        }
    }

    [Test]
    public void SpanEndClosesTheScope()
    {
        var processor = new PyroscopeSpanProcessor();
        using var activity = new Activity("GET /folders").Start();

        processor.OnStart(activity);
        processor.OnEnd(activity);

        Assert.That(AsyncScope.Current, Is.Null);
    }

    [Test]
    public void NestedSpansNestScopes()
    {
        var processor = new PyroscopeSpanProcessor();
        using var request = new Activity("GET /folders").Start();
        processor.OnStart(request);
        try
        {
            using var query = new Activity("DbCommand").Start();
            processor.OnStart(query);
            try
            {
                Assert.That(AsyncScope.Current, Is.EqualTo("DbCommand"));
            }
            finally
            {
                processor.OnEnd(query);
            }

            // Closing the inner span returns to the request's scope rather than clearing
            // everything: the rest of the request still belongs to the request.
            Assert.That(AsyncScope.Current, Is.EqualTo("GET /folders"));
        }
        finally
        {
            processor.OnEnd(request);
        }

        Assert.That(AsyncScope.Current, Is.Null);
    }

    [Test]
    public void FallsBackToTheOperationNameWhenThereIsNoDisplayName()
    {
        var processor = new PyroscopeSpanProcessor();
        // Activity sets DisplayName to OperationName by default; clear it to stand in for
        // a source that leaves it unset.
        using var activity = new Activity("MyOperation").Start();
        activity.DisplayName = string.Empty;

        processor.OnStart(activity);
        try
        {
            Assert.That(AsyncScope.Current, Is.EqualTo("MyOperation"));
        }
        finally
        {
            processor.OnEnd(activity);
        }
    }

    [Test]
    public void ScopesAreNotOpenedWhenStitchingIsTurnedOff()
    {
        var processor = new PyroscopeSpanProcessor(stitchAsyncStacks: false);
        using var activity = new Activity("GET /folders").Start();

        processor.OnStart(activity);
        try
        {
            Assert.That(AsyncScope.Current, Is.Null);
        }
        finally
        {
            processor.OnEnd(activity);
        }
    }

    [Test]
    public void SpanEndIsSafeWhenNoScopeWasOpened()
    {
        var opener = new PyroscopeSpanProcessor(stitchAsyncStacks: false);
        var closer = new PyroscopeSpanProcessor();
        using var activity = new Activity("GET /folders").Start();

        opener.OnStart(activity);

        Assert.DoesNotThrow(() => closer.OnEnd(activity));
        Assert.That(AsyncScope.Current, Is.Null);
    }
}
