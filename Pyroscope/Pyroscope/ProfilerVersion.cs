using System;

namespace Pyroscope
{
    internal static class ProfilerVersion
    {
        public static string ManagedVersion
        {
            get
            {
                var version = typeof(ProfilerVersion).Assembly.GetName().Version;

                return version?.ToString(version.Build >= 0 ? 3 : 2) ?? string.Empty;
            }
        }

        public static bool IsCompatible(string managedVersion, string nativeVersion)
        {
            return Version.TryParse(managedVersion, out var parsedManagedVersion) &&
                   Version.TryParse(nativeVersion, out var parsedNativeVersion) &&
                   parsedManagedVersion.Major == parsedNativeVersion.Major &&
                   parsedManagedVersion.Minor == parsedNativeVersion.Minor;
        }
    }
}
