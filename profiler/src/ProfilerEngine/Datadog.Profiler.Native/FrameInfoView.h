// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once

#include <string>
#include <cstdint>

struct FrameInfoView
{
    std::string_view ModuleName;
    std::string_view Frame;
    std::string_view Filename;
    std::uint32_t StartLine;
};
