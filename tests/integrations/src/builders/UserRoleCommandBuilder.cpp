#include "CommandBuilder.hpp"
#include "CommandUsage.hpp"
#include "generators.hpp"
#include "rbac/role/Admin.hpp"

using namespace vh::test::cli;
using namespace vh::rbac;
using namespace vh::identities;
using namespace vh::vault::model;
using namespace vh::protocols::shell;

UserRoleCommandBuilder::UserRoleCommandBuilder(const std::shared_ptr<protocols::shell::UsageManager>& usage, const std::shared_ptr<CLITestContext>& ctx)
    : CommandBuilder(usage, ctx, "role"), userRoleAliases_(ctx) {}

std::string UserRoleCommandBuilder::updateAndResolveVar(const std::shared_ptr<role::Admin>& entity, const std::string& field) {
    const std::string usagePath = "role/update";

    if (userRoleAliases_.isName(field)) {
        entity->name = generateRoleName(EntityType::USER_ROLE, usagePath);
        return entity->name;
    }

    if (userRoleAliases_.isDescription(field)) {
        entity->description = generateDescription(usagePath);
        return entity->description;
    }

    if (userRoleAliases_.isPermissions(field)) {
        // TODO: Fix perms generation
        // entity->permissions = generateBitmask(ADMIN_SHELL_PERMS.size());
        return entity->toFlagsString();
    }

    throw std::runtime_error("UserRoleCommandBuilder: unsupported user role field for update: " + field);
}

static std::optional<std::string> resolveVar(const std::string& name, const std::shared_ptr<role::Admin>& role) {
    if (name == "id" || name == "role_id") return std::to_string(role->id);
    if (name == "name" || name == "role_name") return role->name;
    if (name == "description" || name == "desc") return role->description;
    if (name == "permissions" || name == "perms") return role->toFlagsString();
    throw std::runtime_error("UserRoleCommandBuilder: unsupported user role field for resolveVar: " + name);
}

static std::string randomizePrimaryPositional(const std::shared_ptr<role::Admin>& entity) {
    if (generateRandomIndex(10000) < 5000) return std::to_string(entity->id);
    return entity->name;
}

std::string UserRoleCommandBuilder::create(const std::shared_ptr<role::Admin>& entity) {
    const auto cmd = root_->findSubcommand("create");
    if (!cmd) throw std::runtime_error("UserRoleCommandBuilder: 'create' command usage not found");

    std::ostringstream oss;
    oss << "vh " << randomAlias(root_->aliases) << ' ' << randomAlias(cmd->aliases) << " user " << entity->name;

    for (const auto& opt : cmd->required) {
        if (opt.label == "type") continue; // skip type, always "user" for user roles
        oss << ' ' << randomFlagAlias(opt.option_tokens);
        if (auto val = resolveVar(opt.option_tokens[0], entity); val) oss << ' ' << *val;
        else throw std::runtime_error("UserRoleCommandBuilder: unsupported user role field for create: " + opt.option_tokens[0]);
    }

    for (const auto& option : cmd->optional) {
        if (coin()) {
            if (option.option_tokens[0] == "from") {
                if (!ctx_->userRoles.empty() && coin())
                    oss << ' ' << randomFlagAlias(option.option_tokens) << ' ' << std::to_string(ctx_->randomUserRole()->id);
                continue;
            }

            if (option.label.contains("description")) {
                oss << ' ' << randomFlagAlias(option.option_tokens) << ' ' << quoted(updateAndResolveVar(entity, option.option_tokens[0]));
                continue;
            }

            oss << ' ' << randomFlagAlias(option.option_tokens);
            if (auto val = resolveVar(option.option_tokens[0], entity); val) oss << ' ' << *val;
            else throw std::runtime_error("UserRoleCommandBuilder: unsupported user role field for create: " + option.option_tokens[0]);
        }
    }

    oss << ' ' << entity->toFlagsString();

    return oss.str();
}

std::string UserRoleCommandBuilder::update(const std::shared_ptr<role::Admin>& entity) {
    const auto cmd = root_->findSubcommand("update");
    if (!cmd) throw std::runtime_error("UserRoleCommandBuilder: 'update' command usage not found");

    std::ostringstream oss;
    oss << "vh " << randomAlias(root_->aliases) << ' ' << randomAlias(cmd->aliases);
    oss << ' ' << randomizePrimaryPositional(entity);

    unsigned int numUpdated = 0;
    for (const auto& opt : cmd->optional) {
        if (coin() || numUpdated == 0) {
            if (opt.label.contains("description")) {
                oss << ' ' << randomFlagAlias(opt.option_tokens) << ' ' << quoted(updateAndResolveVar(entity, opt.option_tokens[0]));
                continue;
            }

            oss << ' ' << randomFlagAlias(opt.option_tokens);
            oss << ' ' << updateAndResolveVar(entity, opt.option_tokens[0]);
            ++numUpdated;
        }
    }

    numUpdated = 0;
    for (const auto& flag : cmd->optional_flags) {
        if (flag.label.contains("filter")) continue;
        if (coin() || numUpdated == 0) {
            if (flag.label.contains("permissions")) oss << ' ' << updateAndResolveVar(entity, flag.label);
            else oss << ' ' << randomFlagAlias(flag.aliases);
            ++numUpdated;
        }
    }

    return oss.str();
}

std::string UserRoleCommandBuilder::info(const std::shared_ptr<role::Admin>& entity) {
    const auto cmd = root_->findSubcommand("info");
    if (!cmd) throw std::runtime_error("UserRoleCommandBuilder: 'info' command usage not found");

    std::ostringstream oss;
    oss << "vh " << randomAlias(root_->aliases) << ' ' << randomAlias(cmd->aliases);
    oss << ' ' << randomizePrimaryPositional(entity);

    return oss.str();
}

std::string UserRoleCommandBuilder::list() {
    const auto cmd = root_->findSubcommand("list");
    if (!cmd) throw std::runtime_error("UserRoleCommandBuilder: 'list' command usage not found");

    std::ostringstream oss;
    oss << "vh " << randomAlias(root_->aliases) << ' ' << randomAlias(cmd->aliases);
    if (generateRandomIndex(10000) < 5000) oss << " --user";
    for (const auto& flag : cmd->optional_flags)
        if (generateRandomIndex(10000) < 5000) oss << " --" << randomAlias(flag.aliases);

    return oss.str();
}

std::string UserRoleCommandBuilder::remove(const std::shared_ptr<role::Admin>& entity) {
    const auto cmd = root_->findSubcommand("delete");
    if (!cmd) throw std::runtime_error("UserRoleCommandBuilder: 'delete' command usage not found");

    std::ostringstream oss;
    oss << "vh " << randomAlias(root_->aliases) << ' ' << randomAlias(cmd->aliases);
    if (generateRandomIndex(10000) < 5000) oss << ' ' << entity->id;
    else oss << ' ' << entity->name << " --user";

    return oss.str();
}

