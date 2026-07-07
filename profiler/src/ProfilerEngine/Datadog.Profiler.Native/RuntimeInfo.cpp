// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "RuntimeInfo.h"
#include <sstream>

RuntimeInfo::RuntimeInfo(uint16_t major, uint16_t minor, bool isFramework)
    :
#ifdef LINUX
    _os("linux"),
#else
    _os("windows"),
#endif
    _major(major),
    _minor(minor),
    _build(0),
    _reviews(0),
    _isFramework(isFramework)
{
}

bool RuntimeInfo::IsDotnetFramework() const
{
    return _isFramework;
}

uint16_t RuntimeInfo::GetMajorVersion() const
{
    return _major;
}

uint16_t RuntimeInfo::GetMinorVersion() const
{
    std::lock_guard<std::mutex> lock(_versionLock);
    return _minor;
}

void RuntimeInfo::SetMinorVersions(uint16_t minor, uint16_t build, uint16_t reviews)
{
    std::lock_guard<std::mutex> lock(_versionLock);
    _minor = minor;
    _build = build;
    _reviews = reviews;
}


std::string RuntimeInfo::GetOs() const
{
    return _os;
}

std::string RuntimeInfo::GetClrString() const
{
    std::lock_guard<std::mutex> lock(_versionLock);
    // runtime_version:
    //    framework-4.8
    //    core-6.0
    std::stringstream buffer;
    if (_isFramework)
    {
        buffer << "framework";
    }
    else
    {
        buffer << "core";
    }
    buffer << "-" << std::dec << _major << "." << _minor;
    if (_build > 0)
    {
        buffer << "." << _build;
    }

    return buffer.str();
}

std::string RuntimeInfo::GetRuntimeName() const
{
    if (_isFramework)
    {
        return ".NET Framework";
    }

    return ".NET";
}

std::string RuntimeInfo::GetRuntimeVersion() const
{
    std::lock_guard<std::mutex> lock(_versionLock);
    std::stringstream buffer;
    buffer << std::dec << _major << "." << _minor;
    if (_build > 0 || _reviews > 0)
    {
        buffer << "." << _build;
    }
    if (_reviews > 0)
    {
        buffer << "." << _reviews;
    }

    return buffer.str();
}
