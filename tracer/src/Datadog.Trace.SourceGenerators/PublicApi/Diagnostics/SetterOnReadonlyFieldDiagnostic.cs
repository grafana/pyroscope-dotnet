// <copyright file="SetterOnReadonlyFieldDiagnostic.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2017 Datadog, Inc.
// </copyright>

using Datadog.Trace.SourceGenerators.Helpers;
using Microsoft.CodeAnalysis;

namespace Datadog.Trace.SourceGenerators.PublicApi.Diagnostics
{
    internal static class SetterOnReadonlyFieldDiagnostic
    {
        internal const string Id = "PA3";
        private const string Message = "You can't generate a setter for a readonly or const field";
        private const string Title = "Setter not supported";

        public static DiagnosticInfo CreateInfo(SyntaxNode? currentNode)
            => new(
                new DiagnosticDescriptor(
                    Id,
                    Title,
                    Message,
                    category: SourceGenerators.Constants.Usage,
                    defaultSeverity: DiagnosticSeverity.Warning,
                    isEnabledByDefault: true),
                currentNode?.GetLocation());
    }
}
