// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once

#include <cstdint>
#include <unistd.h>
#include <sys/syscall.h>

// Async-signal-safe debug utilities.
// These functions only use the write syscall and are safe to call from signal handlers.

namespace SignalHandlerDebug {

// Write a string to stdout (fd 1). Async-signal-safe.
inline void WriteStr(const char* str)
{
    if (str == nullptr) return;

    // Calculate length without using strlen (which may not be async-signal-safe)
    const char* p = str;
    while (*p) ++p;
    std::size_t len = p - str;

    write(1, str, len);
}

// Write a newline to stdout. Async-signal-safe.
inline void WriteNewline()
{
    write(1, "\n", 1);
}

// Write a 64-bit unsigned integer as decimal to stdout. Async-signal-safe.
inline void WriteUInt64(std::uint64_t value)
{
    char buf[21]; // max 20 digits for uint64 + null terminator space (not used)
    char* p = buf + sizeof(buf);

    if (value == 0)
    {
        write(1, "0", 1);
        return;
    }

    while (value > 0)
    {
        --p;
        *p = '0' + (value % 10);
        value /= 10;
    }

    write(1, p, (buf + sizeof(buf)) - p);
}

// Write a 64-bit signed integer as decimal to stdout. Async-signal-safe.
inline void WriteInt64(std::int64_t value)
{
    if (value < 0)
    {
        write(1, "-", 1);
        // Handle INT64_MIN specially to avoid overflow
        if (value == INT64_MIN)
        {
            write(1, "9223372036854775808", 19);
            return;
        }
        value = -value;
    }
    WriteUInt64(static_cast<std::uint64_t>(value));
}

// Write a 64-bit unsigned integer as hexadecimal to stdout. Async-signal-safe.
inline void WriteHex64(std::uint64_t value)
{
    static const char hexDigits[] = "0123456789abcdef";
    char buf[18]; // "0x" + 16 hex digits
    buf[0] = '0';
    buf[1] = 'x';

    if (value == 0)
    {
        write(1, "0x0", 3);
        return;
    }

    char* p = buf + sizeof(buf);
    while (value > 0)
    {
        --p;
        *p = hexDigits[value & 0xF];
        value >>= 4;
    }

    // Write "0x" prefix then the hex digits
    write(1, "0x", 2);
    write(1, p, (buf + sizeof(buf)) - p);
}

// Write a complete debug line: label followed by a 64-bit integer. Async-signal-safe.
inline void WriteLineInt64(const char* label, std::int64_t value)
{
    WriteStr(label);
    WriteInt64(value);
    WriteNewline();
}

// Write a complete debug line: label followed by a 64-bit hex value. Async-signal-safe.
inline void WriteLineHex64(const char* label, std::uint64_t value)
{
    WriteStr(label);
    WriteHex64(value);
    WriteNewline();
}

// Get current thread ID using syscall (async-signal-safe).
inline pid_t GetTid()
{
    return static_cast<pid_t>(syscall(SYS_gettid));
}

// Write current thread ID with label. Async-signal-safe.
inline void WriteThreadId(const char* label = "[tid=")
{
    WriteStr(label);
    WriteInt64(GetTid());
    WriteStr("] ");
}

} // namespace SignalHandlerDebug
