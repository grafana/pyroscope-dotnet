#pragma once

#ifdef _WIN32

#include "windows.h"
#include <process.h>
#include <shellapi.h>

#else

#include <dlfcn.h>
#include <fstream>
#include <unistd.h>

#endif

#if MACOS
#include <crt_externs.h>
#include <libproc.h>
#endif

#include <type_traits>

#include "../../../shared/src/native-src/string.h" // NOLINT
#include "../../../shared/src/native-src/util.h"
#include "dd_filesystem.hpp"
// namespace fs is an alias defined in "dd_filesystem.hpp"

namespace shared
{
inline bool StderrLogEnabled() {
    auto log_to_stderr = GetEnvironmentValue(WStr("PYROSCOPE_LOG_STDERR"));
    bool enabled = false;
    bool parsed = TryParseBooleanEnvironmentValue(log_to_stderr, enabled);
    if (parsed) {
        return enabled;
    }
    return true;
}

template <class T, typename = void>
struct has_deprecated_log_folder : std::false_type
{
};

template <typename T>
struct has_deprecated_log_folder<T, decltype(T::logging_environment::deprecated_log_directory, void())> : std::true_type
{
};

template <class TLoggerPolicy>
inline fs::path GetDefaultLogDir()
{
    bool isAas = false;
    TryParseBooleanEnvironmentValue(GetEnvironmentValue(WStr("DD_AZURE_APP_SERVICES")), isAas);

    if (isAas)
    {
#ifdef _WIN32
        return WStr(R"(C:\home\LogFiles\datadog\)");
#else
        return WStr("/home/LogFiles/datadog/");
#endif
    }

#ifdef _WIN32
    fs::path program_data_path;
    program_data_path = GetEnvironmentValue(WStr("PROGRAMDATA"));

    if (program_data_path.empty())
    {
        program_data_path = WStr(R"(C:\ProgramData)");
    }

    // TODO: Since profiler, tracer, native loader output to the same folder, we
    // can remove the template variable below
    return program_data_path / TLoggerPolicy::folder_path;
#else
    return ToWSTRING("/var/log/pyroscope/dotnet/");
#endif
}

template <class TLoggerPolicy>
inline fs::path GetDatadogLogFilePath(const std::string& file_name_suffix)
{
    const auto file_name = TLoggerPolicy::file_name + file_name_suffix + ".log";

    WSTRING directory;

    if constexpr (has_deprecated_log_folder<TLoggerPolicy>::value)
    {
        // check for deprecated env var first
        directory = GetEnvironmentValue(TLoggerPolicy::logging_environment::deprecated_log_directory);
        if (directory.empty()) directory = GetEnvironmentValue(TLoggerPolicy::logging_environment::log_directory);
    }
    else
        directory = GetEnvironmentValue(TLoggerPolicy::logging_environment::log_directory);

    if (!directory.empty())
    {
        return directory +
#ifdef _WIN32
               WStr('\\') +
#else
               WStr('/') +
#endif
               ToWSTRING(file_name);
    }

    WSTRING path = GetEnvironmentValue(TLoggerPolicy::logging_environment::log_path);

    if (!path.empty())
    {
        return path;
    }

    return GetDefaultLogDir<TLoggerPolicy>() / file_name;
}

inline WSTRING GetCurrentProcessName()
{
#ifdef _WIN32
    const DWORD length = 260;
    WCHAR buffer[length]{};

    const DWORD len = GetModuleFileName(nullptr, buffer, length);
    const WSTRING current_process_path(buffer);
    return fs::path(current_process_path).filename();
#elif MACOS
    const int length = 260;
    char* buffer = new char[length];
    proc_name(getpid(), buffer, length);
    return ToWSTRING(std::string(buffer));
#else
    std::fstream comm("/proc/self/comm");
    std::string name;
    std::getline(comm, name);
    return ToWSTRING(name);
#endif
}

inline std::tuple<WSTRING, std::vector<WSTRING>> GetCurrentProcessCommandLine()
{
    std::vector<WSTRING> args;
#ifdef _WIN32
    const auto cmdLine = GetCommandLine();
    int argCount;
    const auto arguments = CommandLineToArgvW(cmdLine, &argCount);

    for (int i = 0; i < argCount; i++)
    {
        args.push_back(Trim(arguments[i]));
    }

    return { cmdLine, args };
#elif MACOS
    std::string name;
    int argCount = *_NSGetArgc();
    char** arguments = *_NSGetArgv();

    for (int i = 0; i < argCount; i++)
    {
        const auto currentArg = std::string(arguments[i]);
        args.push_back(Trim(ToWSTRING(currentArg)));
        name = name + " " + currentArg;
    }

    return { Trim(ToWSTRING(name)), args };
#else
    std::string cmdline;
    char buf[1024];
    size_t len;
    FILE* fp = fopen("/proc/self/cmdline", "rb");
    if (fp)
    {
        while ((len = fread(buf, 1, sizeof(buf), fp)) > 0)
        {
            cmdline.append(buf, len);
        }

        fclose(fp);
    }

    std::string name;
    std::stringstream tokens(cmdline);
    std::string tmp;
    while (getline(tokens, tmp, '\0'))
    {
        name = name + " " + tmp;
        args.push_back(Trim(ToWSTRING(tmp)));
    }

    return { Trim(ToWSTRING(name)), args };

#endif

    return {EmptyWStr, args};
}

inline int GetPID()
{
#ifdef _WIN32
    return _getpid();
#else
    return getpid();
#endif
}

inline WSTRING GetCurrentModuleFileName()
{
    static WSTRING moduleFileName = EmptyWStr;
    if (moduleFileName != EmptyWStr)
    {
        // use cached version
        return moduleFileName;
    }

#ifdef _WIN32
    HMODULE hModule;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCTSTR) GetCurrentModuleFileName, &hModule))
    {
        WCHAR lpFileName[1024];
        DWORD lpFileNameLength = GetModuleFileNameW(hModule, lpFileName, 1024);
        if (lpFileNameLength > 0)
        {
            moduleFileName = WSTRING(lpFileName, lpFileNameLength);
            return moduleFileName;
        }
    }
#else
    Dl_info info;
    if (dladdr((void*) GetCurrentModuleFileName, &info))
    {
        moduleFileName = ToWSTRING(ToString(info.dli_fname));
        return moduleFileName;
    }
#endif

    return EmptyWStr;
}

inline WSTRING GetProcessStartTime()
{
#if _WIN32
    FILETIME creationTime, exitTime, kernetlTime, userTime;
    if (GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernetlTime, &userTime))
    {
        SYSTEMTIME systemTime;
        if (FileTimeToSystemTime(&creationTime, &systemTime))
        {
            std::ostringstream ossMessage;

            ossMessage << std::setw(2) << std::setfill('0') << systemTime.wDay << "-" << std::setw(2)
                       << std::setfill('0') << systemTime.wMonth << "-" << systemTime.wYear << "_" << std::setw(2)
                       << std::setfill('0') << systemTime.wHour << "-" << std::setw(2) << std::setfill('0')
                       << systemTime.wMinute << "-" << std::setw(2) << std::setfill('0') << systemTime.wSecond;

            return ToWSTRING(ossMessage.str());
        }
    }
    return EmptyWStr;
#else
    return EmptyWStr;
#endif
}

} // namespace shared
