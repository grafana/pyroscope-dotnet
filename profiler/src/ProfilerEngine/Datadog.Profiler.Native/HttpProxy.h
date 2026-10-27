#pragma once

#include "url.hpp"
#include "shared/src/native-src/string.h"

#include <functional>
#include <optional>
#include <string>

struct ProxyConfig
{
    std::string host;
    int port = 0;
    std::string user;
    std::string password;
    // A configured but unusable proxy disables uploads instead of falling back to a direct connection.
    bool invalid = false;
};

class HttpProxy
{
public:
    static std::optional<ProxyConfig> Resolve(const Url& server, const std::function<std::string(const shared::WSTRING&)>& getEnv);
    static bool NoProxyMatches(const std::string& noProxy, const std::string& host, const std::string& port);
};
