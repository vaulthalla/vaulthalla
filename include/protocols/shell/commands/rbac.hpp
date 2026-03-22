#pragma once

#include "protocols/shell/types.hpp"

#include <memory>
#include <regex>

namespace vh::protocols::shell { class Router; }

namespace vh::protocols::shell::commands::rbac {
    namespace permissions {
        void registerCommands(const std::shared_ptr<Router> &r);
    }

    namespace roles {
        void registerCommands(const std::shared_ptr<Router> &r);
        namespace admin {  CommandResult handle_admin_roles(const CommandCall &call); }
        namespace vault {  CommandResult handle_vault_roles(const CommandCall &call); }
    }

    void registerCommands(const std::shared_ptr<Router> &r);
}
