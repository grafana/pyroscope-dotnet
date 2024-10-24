// <copyright file="NpgsqlDatabaseMonitoringTests.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2017 Datadog, Inc.
// </copyright>

using System;
using System.Collections.Generic;
using System.Linq;
using Datadog.Trace.TestHelpers;
using FluentAssertions;
using Xunit;
using Xunit.Abstractions;

namespace Datadog.Trace.ClrProfiler.IntegrationTests.AdoNet
{
    [Trait("RequiresDockerDependency", "true")]
    public class NpgsqlDatabaseMonitoringTests : TracingIntegrationTest
    {
        public NpgsqlDatabaseMonitoringTests(ITestOutputHelper output)
            : base("Npgsql", output)
        {
            SetServiceVersion("1.0.0");
            SetEnvironmentVariable("DD_ENV", "testing");
        }

        public override Result ValidateIntegrationSpan(MockSpan span) => span.IsNpgsql();

        [SkippableTheory]
        [MemberData(nameof(PackageVersions.Npgsql), MemberType = typeof(PackageVersions))]
        [Trait("Category", "EndToEnd")]
        public void SubmitDbmCommentedSpanspropagationNotSet(string packageVersion)
        {
            SubmitDbmCommentedSpans(packageVersion, string.Empty);
        }

        [SkippableTheory]
        [MemberData(nameof(PackageVersions.Npgsql), MemberType = typeof(PackageVersions))]
        [Trait("Category", "EndToEnd")]
        public void SubmitDbmCommentedSpanspropagationIntValue(string packageVersion)
        {
            SubmitDbmCommentedSpans(packageVersion, "100");
        }

        [SkippableTheory]
        [MemberData(nameof(PackageVersions.Npgsql), MemberType = typeof(PackageVersions))]
        [Trait("Category", "EndToEnd")]
        public void SubmitDbmCommentedSpanspropagationUnsupportedValue(string packageVersion)
        {
            SubmitDbmCommentedSpans(packageVersion, "randomValue");
        }

        [SkippableTheory]
        [MemberData(nameof(PackageVersions.Npgsql), MemberType = typeof(PackageVersions))]
        [Trait("Category", "EndToEnd")]
        public void SubmitDbmCommentedSpanspropagationDisabled(string packageVersion)
        {
            SubmitDbmCommentedSpans(packageVersion, "disabled");
        }

        [SkippableTheory]
        [MemberData(nameof(PackageVersions.Npgsql), MemberType = typeof(PackageVersions))]
        [Trait("Category", "EndToEnd")]
        public void SubmitDbmCommentedSpanspropagationService(string packageVersion)
        {
            SubmitDbmCommentedSpans(packageVersion, "service");
        }

        [SkippableTheory]
        [MemberData(nameof(PackageVersions.Npgsql), MemberType = typeof(PackageVersions))]
        [Trait("Category", "EndToEnd")]
        public void SubmitDbmCommentedSpanspropagationFull(string packageVersion)
        {
            SubmitDbmCommentedSpans(packageVersion, "full");
        }

        // Check that the spans have been tagged after the comment was propagated
        private void ValidatePresentDbmTag(IEnumerable<MockSpan> spans, string propagationLevel)
        {
            if (propagationLevel == "service" || propagationLevel == "full")
            {
                foreach (var span in spans)
                {
                    Assert.True(span.Tags?.ContainsKey(Tags.DbmDataPropagated));
                }
            }
            else
            {
                foreach (var span in spans)
                {
                    Assert.False(span.Tags?.ContainsKey(Tags.DbmDataPropagated));
                }
            }
        }

        private void SubmitDbmCommentedSpans(string packageVersion, string propagationLevel)
        {
            if (propagationLevel != string.Empty)
            {
                SetEnvironmentVariable("DD_DBM_PROPAGATION_MODE", propagationLevel);
            }

            // ALWAYS: 77 spans
            // - NpgsqlCommand: 21 spans (3 groups * 7 spans)
            // - DbCommand:  42 spans (6 groups * 7 spans)
            // - IDbCommand: 14 spans (2 groups * 7 spans)
            //
            // NETSTANDARD: +56 spans
            // - DbCommand-netstandard:  42 spans (6 groups * 7 spans)
            // - IDbCommand-netstandard: 14 spans (2 groups * 7 spans)
            //
            // CALLTARGET: +7 spans
            // - IDbCommandGenericConstrant<NpgsqlCommand>: 7 spans (1 group * 7 spans)
            //
            // NETSTANDARD + CALLTARGET: +7 spans
            // - IDbCommandGenericConstrant<NpgsqlCommand>-netstandard: 7 spans (1 group * 7 spans)
            const int expectedSpanCount = 147;
            const string dbType = "postgres";
            const string expectedOperationName = dbType + ".query";

            using var telemetry = this.ConfigureTelemetry();
            using var agent = EnvironmentHelper.GetMockAgent();
            using var process = RunSampleAndWaitForExit(agent, packageVersion: packageVersion);
            var spans = agent.WaitForSpans(expectedSpanCount, operationName: expectedOperationName);
            int actualSpanCount = spans.Count(s => s.ParentId.HasValue && !s.Resource.Equals("SHOW WARNINGS", StringComparison.OrdinalIgnoreCase)); // Remove unexpected DB spans from the calculation

            actualSpanCount.Should().Be(expectedSpanCount);
            ValidatePresentDbmTag(spans, propagationLevel);
        }
    }
}
