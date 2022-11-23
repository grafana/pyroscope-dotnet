// <copyright file="StringExtensions.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2017 Datadog, Inc.
// </copyright>

namespace Pyroscope
{
    internal static class StringExtensions
    {
        /// <summary>
        /// Converts a <see cref="string"/> into a <see cref="bool"/> by comparing it to commonly used values
        /// such as "True", "yes", "T", "Y", or "1" for <c>true</c> and "False", "no", "F", "N", or "0" for <c>false</c>. Case-insensitive.
        /// Defaults to <c>null</c> if string is not recognized.
        /// </summary>
        /// <param name="value">The string to convert.</param>
        /// <returns><c>true</c> or <c>false</c> if <paramref name="value"/> is one of the accepted values; <c>null</c> otherwise.</returns>
        public static bool? ToBoolean(this string value)
        {
            if (value == null) { throw new ArgumentNullException(nameof(value)); }

            if (value.Length == 0)
            {
                return null;
            }

            if (value.Length == 1)
            {
                if (value[0] == 'T' || value[0] == 't' ||
                    value[0] == 'Y' || value[0] == 'y' ||
                    value[0] == '1')
                {
                    return true;
                }

                if (value[0] == 'F' || value[0] == 'f' ||
                    value[0] == 'N' || value[0] == 'n' ||
                    value[0] == '0')
                {
                    return false;
                }

                return null;
            }

            if (string.Equals(value, "TRUE", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(value, "YES", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }

            if (string.Equals(value, "FALSE", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(value, "NO", StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }

            return null;
        }
    }
}
