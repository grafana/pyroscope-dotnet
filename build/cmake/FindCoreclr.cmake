if (ISWINDOWS)
    # MSVC's __uuidof() resolves directly from the MIDL_INTERFACE(...) attributes
    # already present in the vendored corprof.h, so unlike Clang there is no need
    # to compile the classic MIDL-generated corprof_i.cpp companion (which defines
    # IID_*/CLSID_* symbols) - neither Windows vcxproj compiles it either. Common
    # code still needs the vendored coreclr headers (e.g. SymPdbParser.h pulls
    # corsym.h/metahost.h from pal/prebuilt/inc), so keep coreclr as an
    # include-only target on Windows.
    # NOTE: only pal/prebuilt/inc + inc - NOT pal/inc/rt or pal/inc. Those two
    # contain PAL replacements for COM primitives (IUnknown, IID, the `interface`
    # macro, etc.) that exist only because Linux/macOS lack the real Windows SDK.
    # On real Windows they collide with (and shadow) the genuine SDK headers of
    # the same name, causing cascading redefinition/syntax errors. The real
    # Datadog.Profiler.Native.vcxproj confirms this - it only ever adds
    # $(CORECLR-PATH)/pal/prebuilt/inc and $(CORECLR-PATH)/inc.
    add_library(coreclr INTERFACE)

    target_include_directories(coreclr INTERFACE
        ${DOTNET_TRACER_REPO_ROOT_PATH}/shared/src/native-lib/coreclr/src/pal/prebuilt/inc
        ${DOTNET_TRACER_REPO_ROOT_PATH}/shared/src/native-lib/coreclr/src/inc
    )
else()
    add_library(coreclr OBJECT
        ${DOTNET_TRACER_REPO_ROOT_PATH}/shared/src/native-lib/coreclr/src/pal/prebuilt/idl/corprof_i.cpp
    )

    target_include_directories(coreclr PUBLIC
        ${DOTNET_TRACER_REPO_ROOT_PATH}/shared/src/native-lib/coreclr/src/pal/inc/rt
        ${DOTNET_TRACER_REPO_ROOT_PATH}/shared/src/native-lib/coreclr/src/pal/prebuilt/inc
        ${DOTNET_TRACER_REPO_ROOT_PATH}/shared/src/native-lib/coreclr/src/pal/inc
        ${DOTNET_TRACER_REPO_ROOT_PATH}/shared/src/native-lib/coreclr/src/inc
    )

    target_compile_options(coreclr PUBLIC
        -std=c++20
        -DPAL_STDCPP_COMPAT
        -DPLATFORM_UNIX
        -DUNICODE
        -fms-extensions
        -DHOST_64BIT
        -Wno-pragmas
        -g
    )
endif()
