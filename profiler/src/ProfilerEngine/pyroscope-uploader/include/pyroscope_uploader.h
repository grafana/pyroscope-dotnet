#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PyroscopeUploader PyroscopeUploader;

typedef enum PyroscopeLogLevel
{
    PYROSCOPE_LOG_DEBUG = 0,
    PYROSCOPE_LOG_INFO = 1,
    PYROSCOPE_LOG_WARN = 2,
    PYROSCOPE_LOG_ERROR = 3,
} PyroscopeLogLevel;

typedef void (*PyroscopeLogFn)(int32_t level, const char* message, size_t len);

typedef struct PyroscopeStr
{
    const char* data;
    size_t len;
} PyroscopeStr;

typedef struct PyroscopeLabel
{
    PyroscopeStr name;
    PyroscopeStr value;
} PyroscopeLabel;

typedef struct PyroscopeUploaderConfig
{
    PyroscopeStr server_address;
    PyroscopeStr basic_auth_user;
    PyroscopeStr basic_auth_password;
    PyroscopeStr tenant_id;
    PyroscopeStr http_headers_json;
    PyroscopeStr user_agent;
    PyroscopeLogFn log;
} PyroscopeUploaderConfig;

typedef struct PyroscopeProfile
{
    const PyroscopeLabel* labels;
    size_t labels_len;
    const uint8_t* pprof;
    size_t pprof_len;
} PyroscopeProfile;

PyroscopeUploader* pyroscope_uploader_new(const PyroscopeUploaderConfig* config);
void pyroscope_uploader_export(const PyroscopeUploader* uploader, const PyroscopeProfile* profiles, size_t profiles_len);
void pyroscope_uploader_set_basic_auth(const PyroscopeUploader* uploader, PyroscopeStr user, PyroscopeStr password);
void pyroscope_uploader_free(PyroscopeUploader* uploader);

#ifdef __cplusplus
}
#endif
