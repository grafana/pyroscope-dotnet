//
// Created by root on 11/3/22.
//

#include "PyroscopePprofSink.h"
#include "Log.h"
#include "PyroscopeVersion.h"
#include "dd_profiler_version.h"

#include <string_view>

namespace
{
constexpr std::string_view LabelScopeName = "otel.scope.name";
constexpr std::string_view LabelScopeVersion = "otel.scope.version";
constexpr std::string_view LabelProcessRuntimeName = "process.runtime.name";
constexpr std::string_view LabelProcessRuntimeVersion = "process.runtime.version";
constexpr std::string_view PyroscopeDotnetScopeName = "com.grafana.pyroscope/dotnet";
constexpr std::string_view UserAgent = "pyroscope-dotnet/" PYROSCOPE_SPY_VERSION " reqwest";

PyroscopeStr ToPyroscopeStr(std::string_view s)
{
    return PyroscopeStr{s.data(), s.size()};
}

void LogFromUploader(int32_t level, const char* message, size_t len)
{
    std::string msg(message, len);
    switch (level)
    {
        case PYROSCOPE_LOG_DEBUG: Log::Debug("PyroscopePprofSink ", msg); break;
        case PYROSCOPE_LOG_INFO:  Log::Info("PyroscopePprofSink ", msg); break;
        case PYROSCOPE_LOG_WARN:  Log::Warn("PyroscopePprofSink ", msg); break;
        default:                  Log::Error("PyroscopePprofSink ", msg); break;
    }
}

std::string_view ProfileTypeName(ProfileType type)
{
    switch (type)
    {
        case ProfileType::ProcessCpu:     return "process_cpu";
        case ProfileType::CpuSample:      return "process_cpu";
        case ProfileType::GcThreadsCpu:   return "gc";
        case ProfileType::WallTime:       return "wall";
        case ProfileType::Alloc:          return "memory";
        case ProfileType::AllocFramework: return "memory";
        case ProfileType::Lock:           return "lock";
        case ProfileType::Exception:      return "exception";
        case ProfileType::Network:        return "network";
        case ProfileType::Heap:           return "memory";
        case ProfileType::GcCpu:          return "gc";
        case ProfileType::GcStw:          return "gc";
        case ProfileType::ThreadLifetime: return "thread_lifetime";
        default:                          return "unknown";
    }
}
}

PyroscopePprofSink::PyroscopePprofSink(
    std::string server,
    std::string appName,
    BasicAuth basicAuth,
    PyroscopeTenantId tenantId,
    std::string httpHeadersJson,
    IRuntimeInfo* runtimeInfo,
    const Labels& staticTags) :
    _appName(std::move(appName)),
    _runtimeInfo(runtimeInfo),
    _staticTags(staticTags)
{
    PyroscopeUploaderConfig config{
        .server_address = ToPyroscopeStr(server),
        .basic_auth_user = ToPyroscopeStr(basicAuth.user),
        .basic_auth_password = ToPyroscopeStr(basicAuth.password),
        .tenant_id = ToPyroscopeStr(tenantId.value),
        .http_headers_json = ToPyroscopeStr(httpHeadersJson),
        .user_agent = ToPyroscopeStr(UserAgent),
        .log = LogFromUploader,
    };
    _uploader = pyroscope_uploader_new(&config);
}

PyroscopePprofSink::~PyroscopePprofSink()
{
    pyroscope_uploader_free(_uploader);
}

void PyroscopePprofSink::Export(std::vector<Pprof> pprofs)
{
    if (_uploader == nullptr)
    {
        return;
    }
    std::vector<Labels> labels;
    std::vector<std::vector<PyroscopeLabel>> ffiLabels;
    std::vector<PyroscopeProfile> profiles;
    labels.reserve(pprofs.size());
    ffiLabels.reserve(pprofs.size());
    profiles.reserve(pprofs.size());
    for (const auto& pprof : pprofs)
    {
        const auto& profileLabels = labels.emplace_back(GetLabels(pprof.profileType));
        auto& profileFfiLabels = ffiLabels.emplace_back();
        profileFfiLabels.reserve(profileLabels.size());
        for (const auto& [name, value] : profileLabels)
        {
            profileFfiLabels.push_back(PyroscopeLabel{ToPyroscopeStr(name), ToPyroscopeStr(value)});
        }
        profiles.push_back(PyroscopeProfile{
            .labels = profileFfiLabels.data(),
            .labels_len = profileFfiLabels.size(),
            .pprof = reinterpret_cast<const uint8_t*>(pprof.bytes.data()),
            .pprof_len = pprof.bytes.size(),
        });
    }
    pyroscope_uploader_export(_uploader, profiles.data(), profiles.size());
}

void PyroscopePprofSink::SetBasicAuth(BasicAuth basicAuth)
{
    pyroscope_uploader_set_basic_auth(_uploader, ToPyroscopeStr(basicAuth.user), ToPyroscopeStr(basicAuth.password));
}

PyroscopePprofSink::Labels PyroscopePprofSink::GetLabels(ProfileType profileType) const
{
    Labels labels;
    labels.reserve(7 + _staticTags.size());
    labels.emplace_back("__name__", ProfileTypeName(profileType));
    labels.emplace_back("service_name", _appName);
    labels.emplace_back("spy_name", "dotnetspy");
    labels.emplace_back(LabelScopeName, PyroscopeDotnetScopeName);
    labels.emplace_back(LabelScopeVersion, PROFILER_VERSION);
    labels.emplace_back(LabelProcessRuntimeName, _runtimeInfo->GetRuntimeName());
    labels.emplace_back(LabelProcessRuntimeVersion, _runtimeInfo->GetRuntimeVersion());
    labels.insert(labels.end(), _staticTags.begin(), _staticTags.end());
    return labels;
}
