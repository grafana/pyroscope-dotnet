# Overlay for the builtin x64-windows-static triplet that pins the toolset.
# Without the pin, vcpkg builds ports with the newest MSVC it finds; on hosts
# with a newer VS (e.g. GitHub's windows-2025/VS2026 image) the static
# protobuf objects then reference STL helpers (__std_rotate) that the v143
# STL we link against does not export.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
# win-native-test experiment: let CI build the static link deps with the same
# toolset we link the profiler with, so the __std_rotate mismatch above can't
# reappear. Unset -> upstream v143 pin (current behavior).
if(DEFINED ENV{VCPKG_TOOLSET_OVERRIDE} AND NOT "$ENV{VCPKG_TOOLSET_OVERRIDE}" STREQUAL "")
    set(VCPKG_PLATFORM_TOOLSET "$ENV{VCPKG_TOOLSET_OVERRIDE}")
else()
    set(VCPKG_PLATFORM_TOOLSET v143)
endif()
