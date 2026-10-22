find_program(CARGO_EXECUTABLE cargo REQUIRED)

set(PYROSCOPE_UPLOADER_TARGET_DIR ${CMAKE_BINARY_DIR}/pyroscope-uploader)
set(PYROSCOPE_UPLOADER_LIB ${PYROSCOPE_UPLOADER_TARGET_DIR}/release/libpyroscope_uploader.a)
get_filename_component(PYROSCOPE_UPLOADER_OPENSSL_LIB_DIR ${OPENSSL_SSL_LIBRARY} DIRECTORY)
if (USE_STATIC_OPENSSL)
    set(PYROSCOPE_UPLOADER_OPENSSL_STATIC 1)
else()
    set(PYROSCOPE_UPLOADER_OPENSSL_STATIC 0)
endif()

add_custom_target(pyroscope-uploader-cargo
    COMMAND ${CMAKE_COMMAND} -E env
        OPENSSL_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}
        OPENSSL_LIB_DIR=${PYROSCOPE_UPLOADER_OPENSSL_LIB_DIR}
        OPENSSL_STATIC=${PYROSCOPE_UPLOADER_OPENSSL_STATIC}
        ${CARGO_EXECUTABLE} build --release --locked --lib --target-dir ${PYROSCOPE_UPLOADER_TARGET_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    BYPRODUCTS ${PYROSCOPE_UPLOADER_LIB}
    USES_TERMINAL
)

add_library(pyroscope-uploader STATIC IMPORTED GLOBAL)
set_target_properties(pyroscope-uploader PROPERTIES
    IMPORTED_LOCATION ${PYROSCOPE_UPLOADER_LIB}
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(pyroscope-uploader INTERFACE -pthread -ldl -lm -lrt)
if (NOT USE_STATIC_OPENSSL)
    target_link_libraries(pyroscope-uploader INTERFACE OpenSSL::SSL OpenSSL::Crypto)
endif()
add_dependencies(pyroscope-uploader pyroscope-uploader-cargo)
