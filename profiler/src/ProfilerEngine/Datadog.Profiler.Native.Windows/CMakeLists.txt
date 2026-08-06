# ******************************************************
# Project definition
# ******************************************************

project("Datadog.Profiler.Native.Windows" VERSION 3.45.0)

# ******************************************************
# Environment detection
# ******************************************************

SET(PROFILER_SHARED_LIB_NAME Pyroscope.Profiler.Native)

SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${DEPLOY_DIR})
SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${DEPLOY_DIR})
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${DEPLOY_DIR})

# ******************************************************
# Sources
# ******************************************************

# Own-directory Windows-specific sources, mirroring Datadog.Profiler.Native.Windows.vcxproj's
# explicit ClCompile list. Deliberately excluded:
#  - CrashReportingWindows.cpp: <ExcludedFromBuild>true</ExcludedFromBuild> in the vcxproj too
#    (kept only for clean upstream merges; crash reporting isn't shipped in this fork).
#  - Timer.Windows.cpp (this directory's copy): stale duplicate of the common-dir file of the
#    same name, not referenced by any vcxproj. The common-dir Timer.Windows.cpp (picked up via
#    COMMON_PROFILER_SRC below) is the one the real Windows build actually uses.
FILE(GLOB WINDOWS_PROFILER_SRC CONFIGURE_DEPENDS "*.cpp")
list(REMOVE_ITEM WINDOWS_PROFILER_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/CrashReportingWindows.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Timer.Windows.cpp"
)
FILE(GLOB WINDOWS_ETW_SRC CONFIGURE_DEPENDS "ETW/*.cpp")

FILE(GLOB COMMON_PROFILER_SRC LIST_DIRECTORIES false "../Datadog.Profiler.Native/*.cpp")
FILE(GLOB_RECURSE PROTO_PROFILER_SRC CONFIGURE_DEPENDS "../Datadog.Profiler.Native/gen/*.pb.cc")

# ******************************************************
# Define shared target
# ******************************************************

add_library(${PROFILER_SHARED_LIB_NAME} SHARED
    ${WINDOWS_PROFILER_SRC}
    ${WINDOWS_ETW_SRC}
    ${COMMON_PROFILER_SRC}
    ${PROTO_PROFILER_SRC}
    ${DOTNET_TRACER_REPO_ROOT_PATH}/shared/src/native-src/string.cpp
    ${DOTNET_TRACER_REPO_ROOT_PATH}/shared/src/native-src/util.cpp
    ${DOTNET_TRACER_REPO_ROOT_PATH}/shared/src/native-src/miniutf.cpp
    Resource.rc
)

set_target_properties(${PROFILER_SHARED_LIB_NAME} PROPERTIES PREFIX "")

target_include_directories(${PROFILER_SHARED_LIB_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ETW
    ${CMAKE_CURRENT_SOURCE_DIR}/../Datadog.Profiler.Native
    ${CMAKE_CURRENT_SOURCE_DIR}/../Datadog.Profiler.Native/gen
    ${DOTNET_TRACER_REPO_ROOT_PATH}
    ${DOTNET_TRACER_REPO_ROOT_PATH}/profiler/third_party/cpp-httplib
)

target_compile_definitions(${PROFILER_SHARED_LIB_NAME} PRIVATE
    WIN32
    BIT64
    DATADOGAUTOINSTRUMENTATIONPROFILERNATIVEWINDOWS_EXPORTS
    _WINDOWS
    _USRDLL
    NOMINMAX
    _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
    MICROSOFT_WINDOWS_WINBASE_H_DEFINE_INTERLOCKED_CPLUSPLUS_OVERLOADS=1
    CPPHTTPLIB_OPENSSL_SUPPORT
    # NOTE: deliberately no CPPHTTPLIB_USE_POLL - that's POSIX-only (poll()), used only by
    # the Linux CMakeLists; cpp-httplib's Windows path doesn't need or support it.
)

target_compile_options(${PROFILER_SHARED_LIB_NAME} PRIVATE
    /utf-8
    "/FI${DOTNET_TRACER_REPO_ROOT_PATH}/profiler/src/ProfilerEngine/Datadog.Profiler.Native/PyroscopeWindowsIncludeOrder.h"
)

# .def file controls DLL exports (DllMain/DllGetClassObject/DllCanUnloadNow + fork API),
# equivalent of the vcxproj's <ModuleDefinitionFile>.
set(dd_profiling_def_file "${CMAKE_CURRENT_SOURCE_DIR}/Datadog.Profiler.Native.def")
target_link_options(${PROFILER_SHARED_LIB_NAME} PRIVATE "/DEF:${dd_profiling_def_file}")
set_target_properties(${PROFILER_SHARED_LIB_NAME} PROPERTIES LINK_DEPENDS "${dd_profiling_def_file}")

target_link_libraries(${PROFILER_SHARED_LIB_NAME} PRIVATE
    coreclr
    PPDB
    spdlog-headers
    libprotobuf
    chmike::CxxUrl
    OpenSSL::Crypto
    OpenSSL::SSL
    dbghelp
    ole32
    mscoree
    ws2_32
)

add_dependencies(${PROFILER_SHARED_LIB_NAME} spdlog-headers PPDB)
