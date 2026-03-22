#include "protocols/shell/commands/rbac.hpp"
#include "protocols/shell/Router.hpp"
#include "UsageManager.hpp"
#include "runtime/Deps.hpp"

namespace vh::protocols::shell::commands::rbac {
    void registerCommands(const std::shared_ptr<Router> &r) {
        permissions::registerCommands(r);
        roles::registerCommands(r);
    }
}
