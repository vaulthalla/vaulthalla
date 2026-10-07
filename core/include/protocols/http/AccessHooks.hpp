#pragma once

#include "protocols/http/Access.hpp"

#include <functional>

// Injection points the unit tests use (Router::set*ForTesting delegates here).
namespace vh::protocols::http::access::hooks {

using SessionResolver = std::function<std::shared_ptr<ws::Session>(const Request&)>;
using ShareManagerFactory = std::function<std::shared_ptr<share::Manager>()>;
using ShareResolverFactory = std::function<std::shared_ptr<share::TargetResolver>()>;
using EngineResolver = std::function<std::shared_ptr<storage::Engine>(uint32_t)>;

SessionResolver& sessionResolver();
ShareManagerFactory& shareManager();
ShareResolverFactory& shareResolver();
EngineResolver& engineResolver();

void resetSessionResolver();
void resetShareManager();
void resetShareResolver();
void resetEngineResolver();

}
