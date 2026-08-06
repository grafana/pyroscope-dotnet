SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY PPDB_build)
SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY PPDB_build)
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY PPDB_build)

add_library(PPDB STATIC
  ${DOTNET_TRACER_REPO_ROOT_PATH}/shared/src/native-lib/PPDB/Reader/CoreReader.cpp
  ${DOTNET_TRACER_REPO_ROOT_PATH}/shared/src/native-lib/PPDB/Reader/Streams.cpp
  ${DOTNET_TRACER_REPO_ROOT_PATH}/shared/src/native-lib/PPDB/Reader/Tables.cpp)

if (ISWINDOWS)
    # No -DPLATFORM_UNIX on Windows; no -fms-extensions equivalent needed since
    # those MS-specific extensions are native under real cl.exe.
    target_compile_options(PPDB PUBLIC /std:c++20 /DPAL_STDCPP_COMPAT /DUNICODE)
else()
    # Sets compiler options
    target_compile_options(PPDB PRIVATE -std=c++20 -fPIC -fms-extensions)
    target_compile_options(PPDB PUBLIC -DPAL_STDCPP_COMPAT -DPLATFORM_UNIX -DUNICODE)
    target_compile_options(PPDB PRIVATE -Wno-invalid-noreturn -Wno-macro-redefined)
endif()

target_include_directories(PPDB
   PUBLIC ${DOTNET_TRACER_REPO_ROOT_PATH}/shared/src/native-lib/PPDB/inc)
