// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "ProviderBase.h"
#include "Log.h"
#include "Sample.h"

ProviderBase::ProviderBase(ProfileType type, const char* name)
    :
    _name{name},
    _type{type}
{
}

const char* ProviderBase::GetName()
{
    return _name.c_str();
}
