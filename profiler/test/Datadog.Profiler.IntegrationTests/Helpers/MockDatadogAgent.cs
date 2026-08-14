// <copyright file="MockDatadogAgent.cs" company="Grafana Labs">
// Licensed under the Apache 2 License.
// </copyright>

using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Threading;
using Google.Protobuf;
using Xunit.Abstractions;

namespace Datadog.Profiler.IntegrationTests
{
    /// <summary>
    /// Compatibility facade retained to minimize divergence from the upstream tests.
    /// The fork sends Pyroscope Push API requests rather than Datadog agent requests.
    /// </summary>
    public abstract class MockDatadogAgent : IDisposable
    {
        private readonly ManualResetEventSlim _readinessNotifier = new();
        private int _nbCallsOnProfilingEndpoint;

        public event EventHandler<EventArgs<HttpListenerContext>> ProfilerRequestReceived;

        public int NbCallsOnProfilingEndpoint => Volatile.Read(ref _nbCallsOnProfilingEndpoint);

        public int ProfiledProcessId { get; set; }

        public bool IsReady => _readinessNotifier.Wait(TimeSpan.FromSeconds(30));

        public static HttpAgent CreateHttpAgent(ITestOutputHelper output, int retries = 5) => new(output, retries);

        public static NamedPipeAgent CreateNamedPipeAgent(ITestOutputHelper output) => new(output);

        public virtual void Dispose()
        {
            _readinessNotifier.Dispose();
        }

        protected void MarkReady() => _readinessNotifier.Set();

        protected void MarkProfileReceived(HttpListenerContext context)
        {
            ProfilerRequestReceived?.Invoke(this, new EventArgs<HttpListenerContext>(context));
            Interlocked.Increment(ref _nbCallsOnProfilingEndpoint);
        }

        public sealed class HttpAgent : MockDatadogAgent
        {
            private const string ProfilesEndpoint = "/push.v1.PusherService/Push";
            private readonly Thread _listenerThread;
            private HttpListener _listener;
            private int _profileSequence;

            public HttpAgent(ITestOutputHelper output, int retries)
            {
                Initialize(retries);
                _listenerThread = new Thread(HandleHttpRequests)
                {
                    IsBackground = true,
                    Name = "Pyroscope test receiver",
                };
                _listenerThread.Start();
            }

            public int Port { get; private set; }

            public string ProfilesDirectory { get; set; }

            public override void Dispose()
            {
                _listener?.Stop();
                _listenerThread.Join(TimeSpan.FromSeconds(5));
                _listener?.Close();
                base.Dispose();
            }

            private void Initialize(int retries)
            {
                var port = Helpers.TcpPortProvider.GetOpenPort();
                while (true)
                {
                    var listener = new HttpListener();
                    listener.Prefixes.Add($"http://127.0.0.1:{port}/");

                    try
                    {
                        listener.Start();
                        Port = port;
                        _listener = listener;
                        return;
                    }
                    catch (HttpListenerException) when (retries-- > 0)
                    {
                        listener.Close();
                        port = Helpers.TcpPortProvider.GetOpenPort();
                    }
                }
            }

            private void HandleHttpRequests()
            {
                MarkReady();
                while (_listener.IsListening)
                {
                    try
                    {
                        var context = _listener.GetContext();
                        if (context.Request.Url?.AbsolutePath == ProfilesEndpoint)
                        {
                            SaveProfiles(context.Request.InputStream);
                            MarkProfileReceived(context);
                        }

                        context.Response.StatusCode = (int)HttpStatusCode.OK;
                        context.Response.ContentLength64 = 0;
                        context.Response.Close();
                    }
                    catch (HttpListenerException)
                    {
                        return;
                    }
                    catch (ObjectDisposedException)
                    {
                        return;
                    }
                    catch (Exception)
                    {
                        // A failed receiver makes profile assertions fail without taking down
                        // the entire test host from this background thread.
                        return;
                    }
                }
            }

            private void SaveProfiles(Stream requestBody)
            {
                if (string.IsNullOrWhiteSpace(ProfilesDirectory))
                {
                    throw new InvalidOperationException("The profile output directory was not configured.");
                }

                Directory.CreateDirectory(ProfilesDirectory);
                foreach (var profile in PyroscopePushReader.ReadRawProfiles(requestBody))
                {
                    var sequence = Interlocked.Increment(ref _profileSequence);
                    var path = Path.Combine(ProfilesDirectory, $"profile-{sequence:D6}.pprof");
                    File.WriteAllBytes(path, profile);
                }
            }
        }

        public sealed class NamedPipeAgent : MockDatadogAgent
        {
            public NamedPipeAgent(ITestOutputHelper output)
            {
                throw new NotSupportedException("Named-pipe transport is not supported by the Pyroscope exporter.");
            }

            public string ProfilesPipeName => string.Empty;
        }

        internal static class DatadogHttpValues
        {
            public const char CarriageReturn = '\r';
            public const char LineFeed = '\n';
            public const string CrLf = "\r\n";
        }

        private static class PyroscopePushReader
        {
            public static IEnumerable<byte[]> ReadRawProfiles(Stream stream)
            {
                using var input = new CodedInputStream(stream, leaveOpen: true);
                while (input.ReadTag() is var tag && tag != 0)
                {
                    if (tag == 10) // PushRequest.series
                    {
                        foreach (var profile in ReadSeries(input.ReadBytes()))
                        {
                            yield return profile;
                        }
                    }
                    else
                    {
                        input.SkipLastField();
                    }
                }
            }

            private static IEnumerable<byte[]> ReadSeries(ByteString bytes)
            {
                using var input = new CodedInputStream(bytes.ToByteArray());
                while (input.ReadTag() is var tag && tag != 0)
                {
                    if (tag == 18) // RawProfileSeries.samples
                    {
                        var profile = ReadSample(input.ReadBytes());
                        if (profile != null)
                        {
                            yield return profile;
                        }
                    }
                    else
                    {
                        input.SkipLastField();
                    }
                }
            }

            private static byte[] ReadSample(ByteString bytes)
            {
                using var input = new CodedInputStream(bytes.ToByteArray());
                while (input.ReadTag() is var tag && tag != 0)
                {
                    if (tag == 10) // RawSample.raw_profile
                    {
                        return input.ReadBytes().ToByteArray();
                    }

                    input.SkipLastField();
                }

                return null;
            }
        }
    }
}
