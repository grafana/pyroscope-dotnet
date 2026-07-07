// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once

#include "IRuntimeInfo.h"

#include <mutex>

class RuntimeInfo : public IRuntimeInfo
{
public:
    RuntimeInfo(uint16_t major, uint16_t minor, bool isFramework);

    // Inherited via IRuntimeInfo
    bool IsDotnetFramework() const override;
    uint16_t GetMajorVersion() const override;
    uint16_t GetMinorVersion() const override;
    std::string GetOs() const override;
    std::string GetClrString() const override;
    std::string GetRuntimeName() const override;
    std::string GetRuntimeVersion() const override;
    void SetMinorVersions(uint16_t minor, uint16_t build, uint16_t reviews) override;

private:
    mutable std::mutex _versionLock;
    uint16_t _major;
    uint16_t _minor;
    uint16_t _build;
    uint16_t _reviews;
    bool _isFramework;
    std::string _os;
};
