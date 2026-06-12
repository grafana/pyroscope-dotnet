# Overlay for the builtin x64-windows-static triplet that pins the toolset.
# Without the pin, vcpkg builds ports with the newest MSVC it finds; on hosts
# with a newer VS (e.g. GitHub's windows-2025/VS2026 image) the static
# protobuf objects then reference STL helpers (__std_rotate) that the v143
# STL we link against does not export.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET v143)
