using System.Reflection;

namespace Pyroscope
{
    internal static class ProfilerVersion
    {
        public static string ManagedVersion
        {
            get
            {
                var informationalVersion = typeof(ProfilerVersion).Assembly
                    .GetCustomAttribute<AssemblyInformationalVersionAttribute>()?
                    .InformationalVersion;

                return informationalVersion ?? string.Empty;
            }
        }

        public static bool IsCompatible(string managedVersion, string nativeVersion)
        {
            return !string.IsNullOrEmpty(managedVersion) &&
                   !string.IsNullOrEmpty(nativeVersion) &&
                   string.Equals(managedVersion, nativeVersion, StringComparison.Ordinal);
        }
    }
}
