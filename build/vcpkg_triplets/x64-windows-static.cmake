# Overlay for the builtin x64-windows-static triplet that pins the toolset.
# vcpkg must build the static deps (openssl, protobuf) with the SAME toolset we
# link the profiler with, or the static objects reference STL helpers (e.g.
# __std_rotate) the link toolset's STL may not export. CI builds with whatever
# toolset the runner image ships and passes it here via VCPKG_TOOLSET_OVERRIDE;
# unset, we fall back to v143 (the upstream vcxproj pin).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
if(DEFINED ENV{VCPKG_TOOLSET_OVERRIDE} AND NOT "$ENV{VCPKG_TOOLSET_OVERRIDE}" STREQUAL "")
    set(VCPKG_PLATFORM_TOOLSET "$ENV{VCPKG_TOOLSET_OVERRIDE}")
else()
    set(VCPKG_PLATFORM_TOOLSET v143)
endif()
