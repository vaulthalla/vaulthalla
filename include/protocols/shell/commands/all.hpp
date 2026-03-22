#pragma once

#include <memory>

namespace vh::protocols::shell { class Router; }

namespace vh::protocols::shell::commands {

void registerAllCommands(const std::shared_ptr<Router>& r);

void registerAPIKeyCommands(const std::shared_ptr<Router>& r);
void registerSystemCommands(const std::shared_ptr<Router>& r);
void registerUserCommands(const std::shared_ptr<Router>& r);
void registerGroupCommands(const std::shared_ptr<Router>& r);
void registerSecretsCommands(const std::shared_ptr<Router>& r);

}
