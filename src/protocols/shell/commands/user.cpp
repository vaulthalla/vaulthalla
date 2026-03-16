#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/Router.hpp"
#include "db/query/identities/User.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "crypto/util/hash.hpp"
#include "log/Registry.hpp"
#include "rbac/role/Admin.hpp"
#include "runtime/Deps.hpp"
#include "usage/include/UsageManager.hpp"
#include "CommandUsage.hpp"
#include "auth/registration/Validator.hpp"
#include "identities/User.hpp"

#include <paths.h>

using namespace vh;
using namespace vh::protocols::shell;
using namespace vh::rbac::role;
using namespace vh::identities;
using namespace vh::auth;
using namespace vh::crypto;

static const unsigned int PASSWORD_LENGTH = vh::paths::testMode ? 8 : 84;

static std::string tryAssignNewPassword(const std::shared_ptr<User>& user) {
    constexpr unsigned short maxRetries = 1024 * 4; // 4096 attempts max
    for (unsigned short i = 1; i < maxRetries; ++i) {
        if (const auto password = hash::generate_secure_password(PASSWORD_LENGTH); registration::Validator::isValidPassword(password)) {
            user->setPasswordHash(hash::password(password));
            return password;
        }
        if (i == maxRetries)
            throw std::runtime_error("Failed to generate a valid password after " + std::to_string(maxRetries) + " attempts");
    }
    throw std::runtime_error("Failed to generate a valid password");
}

static void assignEmail(const CommandCall& call, const std::shared_ptr<User>& user, const std::shared_ptr<CommandUsage>& usage) {
    if (const auto emailOpt = optVal(call, usage->resolveOptional("email")->option_tokens)) {
        if (!registration::Validator::isValidEmail(*emailOpt))
            throw std::runtime_error("Invalid email address: " + *emailOpt);
        user->email = *emailOpt;
    }
}

static void assignLinuxUidIfAvailable(const CommandCall& call, const std::shared_ptr<User>& user, const std::shared_ptr<CommandUsage>& usage) {
    if (const auto linuxUidOpt = optVal(call, usage->resolveOptional("linux-uid")->option_tokens)) {
        const auto parsed = parseUInt(*linuxUidOpt);
        if (!parsed || *parsed <= 0)
            throw std::runtime_error("Invalid --linux-uid: must be a positive integer");
        user->meta.linux_uid = *parsed;
    }
}

static CommandResult createUser(const CommandCall& call) {
    constexpr const auto* ERR = "user create";

    const auto usage = resolveUsage({"user", "create"});
    validatePositionals(call, usage);

    const auto user = std::make_shared<User>();
    user->name = call.positionals[0];
    assignEmail(call, user, usage);
    assignLinuxUidIfAvailable(call, user, usage);
    user->roles.admin = std::make_shared<Admin>();
    user->meta.updated_by = call.user->id;

    if (!registration::Validator::isValidName(user->name)) return invalid("Invalid user name: " + user->name);

    const auto roleOpt = optVal(call, usage->resolveRequired("role")->option_tokens);
    if (!roleOpt) return invalid("user create: --role is required");

    const auto rLkp = resolveAdminRole(*roleOpt, ERR);
    if (!rLkp || !rLkp.ptr) return invalid(rLkp.error);
    const auto role = rLkp.ptr;

    user->roles.admin = role;

    if (user->isSuperAdmin())
        return invalid("Cannot create user with super_admin role.");

    if (user->isAdmin() && !call.user->admins().canAdd())
        return invalid("You do not have permission to create admin users.");

    if (!user->isAdmin() && !call.user->users().canAdd())
        return invalid("You do not have permission to create users.");

    const auto password = tryAssignNewPassword(user);
    user->id = db::query::identities::User::createUser(user);

    std::string out = "User created successfully: " + to_string(user) + "\n";
    out += "Password: " + password + "\n";
    return ok(out);
}

static CommandResult handleUpdateUser(const CommandCall& call) {
    constexpr const auto* ERR = "user update";

    const auto usage = resolveUsage({"user", "update"});
    validatePositionals(call, usage);

    const auto uLkp = resolveUser(call.positionals[0], ERR);
    if (!uLkp || !uLkp.ptr) return invalid(uLkp.error);
    const auto user = uLkp.ptr;

    if (call.user->id != user->id) {
        if (user->isSuperAdmin())
            return invalid("Cannot update super admin user: " + user->name);

        if (user->isAdmin() && !call.user->admins().canEdit())
            return invalid("You do not have permission to update admin users.");

        if (!user->isAdmin() && !call.user->users().canEdit())
            return invalid("You do not have permission to update users.");
    }

    if (const auto newNameOpt = optVal(call, usage->resolveOptional("name")->option_tokens)) {
        if (user->isSuperAdmin()) return invalid("Cannot change name of super_admin user: " + user->name);
        if (!registration::Validator::isValidName(*newNameOpt)) return invalid("Invalid new user name: " + *newNameOpt);
        user->name = *newNameOpt;
    }

    if (const auto newRoleOpt = optVal(call, usage->resolveOptional("role")->option_tokens)) {
        if (user->isSuperAdmin()) return invalid("Cannot change role of super_admin user: " + user->name);
        if (*newRoleOpt == "super_admin") return invalid("Cannot change role to super_admin.");

        const auto rLkp = resolveAdminRole(*newRoleOpt, ERR);
        if (!rLkp || !rLkp.ptr) return invalid(rLkp.error);
        const auto role = rLkp.ptr;
        user->roles.admin = role;
    }

    assignEmail(call, user, usage);
    assignLinuxUidIfAvailable(call, user, usage);

    user->meta.updated_by = call.user->id;

    db::query::identities::User::updateUser(user);

    return ok("User updated successfully: " + user->name + "\n" + to_string(user));
}

static CommandResult deleteUser(const CommandCall& call) {
    const auto usage = resolveUsage({"user", "delete"});
    validatePositionals(call, usage);

    const auto uLkp = resolveUser(call.positionals[0], "user delete");
    if (!uLkp || !uLkp.ptr) return invalid(uLkp.error);
    const auto user = uLkp.ptr;

    if (user->isSuperAdmin()) {
        log::Registry::audit()->warn("[UserCommands] Attempt to delete super_admin user: {}, by user: {}",
            user->name, call.user->name);
        log::Registry::shell()->warn("[UserCommands] Attempt to delete super_admin user: {}, by user: {}",
            user->name, call.user->name);
        log::Registry::vaulthalla()->warn("[UserCommands] Attempt to delete super_admin user: {}, by user: {}",
            user->name, call.user->name);
        return invalid("Cannot delete super admin user: " + user->name);
    }

    if (call.user->id != user->id) {
        if (user->isSuperAdmin())
            return invalid("Cannot delete super admin user: " + user->name);

        if (user->isAdmin() && !call.user->admins().canDelete())
            return invalid("You do not have permission to delete admin users.");

        if (!user->isAdmin() && !call.user->users().canDelete())
            return invalid("You do not have permission to delete users.");
    }

    db::query::identities::User::deleteUser(user->id);
    return ok("User deleted successfully: " + user->name);
}

static CommandResult handleUserInfo(const CommandCall& call) {
    constexpr const auto* ERR = "user info";

    const auto usage = resolveUsage({"user", "info"});
    validatePositionals(call, usage);

    const auto uLkp = resolveUser(call.positionals[0], ERR);
    if (!uLkp || !uLkp.ptr) return invalid(uLkp.error);
    const auto user = uLkp.ptr;

    if (call.user->id != user->id) {
        if (user->isSuperAdmin() && !call.user->admins().canView())
            return invalid("You do not have permission to view super admin users.");

        if (user->isAdmin() && !call.user->admins().canView())
            return invalid("You do not have permission to view admin users.");

        if (!user->isAdmin() && !call.user->users().canView())
            return invalid("You do not have permission to view users.");
    }

    return ok(to_string(user));
}

static CommandResult handle_list_users(const CommandCall& call) {
    if (!call.user->admins().canView())
        return invalid("You do not have permission to list users.");

    return ok(to_string(db::query::identities::User::listUsers(parseListQuery(call))));
}

static bool isUserMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"user", cmd}, input);
}

static CommandResult handle_user(const CommandCall& call) {
    if (call.positionals.empty() || hasFlag(call, "h") || hasFlag(call, "help"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);

    if (isUserMatch("create", sub)) return createUser(subcall);
    if (isUserMatch("delete", sub)) return deleteUser(subcall);
    if (isUserMatch("info", sub)) return handleUserInfo(subcall);
    if (isUserMatch("update", sub)) return handleUpdateUser(subcall);
    if (isUserMatch("list", sub) || isUserMatch("ls", sub)) return handle_list_users(subcall);

    return invalid(call.constructFullArgs(), "Unknown user subcommand: '" + std::string(sub) + "'");
}

void commands::registerUserCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("user"), handle_user);
}
