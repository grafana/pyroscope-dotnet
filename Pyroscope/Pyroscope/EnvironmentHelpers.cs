// <copyright file="EnvironmentHelpers.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2017 Datadog, Inc.
// </copyright>

namespace Pyroscope
{
    /// <summary>
    /// Helpers to access environment variables
    /// </summary>
    internal static class EnvironmentHelpers
    {


        /// <summary>
        /// Safe wrapper around Environment.GetEnvironmentVariable
        /// </summary>
        /// <param name="key">Name of the environment variable to fetch</param>
        /// <param name="defaultValue">Value to return in case of error</param>
        /// <returns>The value of the environment variable, or the default value if an error occured</returns>
        public static string? GetEnvironmentVariable(string key, string? defaultValue = null)
        {
            var keys = new List<string>();
            keys.Add(key);
            if (key.StartsWith("DD_"))
            {
                var baseName = key.Substring(3);
                keys.Add(baseName);
                keys.Add("PYROSCOPE_" + baseName);
            }

            var error = false;
            for (var i = 0; i < keys.Count; i++)
            {
                try
                {
                    var val = Environment.GetEnvironmentVariable(keys[i]);
                    if (val != null)
                    {
                        return val;
                    }
                }
                catch (Exception)
                {
                    error = true;
                }
            }
            if (error)
            {
                return defaultValue;
            }
            return null;
        }

       
    }
}
