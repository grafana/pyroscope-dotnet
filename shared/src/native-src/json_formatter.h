#pragma once

// A spdlog formatter that emits one JSON object per line, shaped for log
// collectors that parse structured records straight off the container's
// stdout/stderr (Google Cloud Logging's agent, among others).
//
// It is applied to the stderr sink only. The rotating file sink keeps the
// human-readable pattern, because nothing parses that file.

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>

#include <spdlog/details/log_msg.h>
#include <spdlog/details/os.h>
#include <spdlog/formatter.h>

namespace datadog::shared
{

/// Maps an spdlog level onto the severity names a structured log collector
/// understands. spdlog has no "notice", and "off" never reaches a formatter,
/// so the mapping is total over the levels that can be emitted.
inline const char* ToSeverity(spdlog::level::level_enum level)
{
    switch (level)
    {
        case spdlog::level::trace:
        case spdlog::level::debug:
            return "DEBUG";
        case spdlog::level::info:
            return "INFO";
        case spdlog::level::warn:
            return "WARNING";
        case spdlog::level::err:
            return "ERROR";
        case spdlog::level::critical:
            return "CRITICAL";
        default:
            return "DEFAULT";
    }
}

/// Appends `value` to `dest` as the body of a JSON string, escaped per RFC 8259.
/// Bytes above 0x1f are passed through untouched, which keeps valid UTF-8 intact
/// and avoids re-encoding log payloads we do not control.
inline void AppendEscaped(spdlog::memory_buf_t& dest, std::string_view value)
{
    for (const char c : value)
    {
        switch (c)
        {
            case '"':
                dest.append(std::string_view("\\\""));
                break;
            case '\\':
                dest.append(std::string_view("\\\\"));
                break;
            case '\b':
                dest.append(std::string_view("\\b"));
                break;
            case '\f':
                dest.append(std::string_view("\\f"));
                break;
            case '\n':
                dest.append(std::string_view("\\n"));
                break;
            case '\r':
                dest.append(std::string_view("\\r"));
                break;
            case '\t':
                dest.append(std::string_view("\\t"));
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    std::array<char, 7> buffer{};
                    std::snprintf(buffer.data(), buffer.size(), "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(c)));
                    dest.append(std::string_view(buffer.data()));
                }
                else
                {
                    dest.push_back(c);
                }
        }
    }
}

/// Renders an spdlog timestamp as RFC 3339 with an explicit UTC offset.
///
/// spdlog hands us a std::chrono::system_clock::time_point, which is measured
/// from the Unix epoch and therefore already UTC. The zone is not inferred from
/// the host, so this is safe to emit as `Z`.
///
/// Emitted under the key "time". Collectors that special-case a timestamp
/// commonly expect an RFC 3339 string there and reserve "timestamp" for a
/// structured seconds/nanos object, so a string under "timestamp" would be the
/// wrong type for the field it looks like it is filling.
inline void AppendTimestamp(spdlog::memory_buf_t& dest, std::chrono::system_clock::time_point time)
{
    const auto since_epoch = time.time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch - seconds);

    const std::time_t as_time_t = static_cast<std::time_t>(seconds.count());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &as_time_t);
#else
    gmtime_r(&as_time_t, &utc);
#endif

    std::array<char, 32> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                                      utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min,
                                      utc.tm_sec, static_cast<int>(millis.count()));
    if (written > 0)
    {
        dest.append(std::string_view(buffer.data(), static_cast<size_t>(written)));
    }
}

class JsonFormatter final : public spdlog::formatter
{
public:
    void format(const spdlog::details::log_msg& msg, spdlog::memory_buf_t& dest) override
    {
        dest.append(std::string_view(R"({"severity":")"));
        dest.append(std::string_view(ToSeverity(msg.level)));

        dest.append(std::string_view(R"(","time":")"));
        AppendTimestamp(dest, msg.time);

        dest.append(std::string_view(R"(","message":")"));
        AppendEscaped(dest, std::string_view(msg.payload.data(), msg.payload.size()));

        dest.append(std::string_view(R"(","logger":")"));
        AppendEscaped(dest, std::string_view(msg.logger_name.data(), msg.logger_name.size()));

        dest.append(std::string_view(R"(","pid":)"));
        AppendUnsigned(dest, static_cast<unsigned long long>(spdlog::details::os::pid()));

        dest.append(std::string_view(R"(,"tid":)"));
        AppendUnsigned(dest, static_cast<unsigned long long>(msg.thread_id));

        dest.push_back('}');
        dest.append(std::string_view(spdlog::details::os::default_eol));
    }

    std::unique_ptr<spdlog::formatter> clone() const override
    {
        return std::make_unique<JsonFormatter>();
    }

private:
    static void AppendUnsigned(spdlog::memory_buf_t& dest, unsigned long long value)
    {
        const std::string rendered = std::to_string(value);
        dest.append(std::string_view(rendered));
    }
};

} // namespace datadog::shared
