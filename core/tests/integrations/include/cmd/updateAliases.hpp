#pragma once

#include "types/Type.hpp"
#include "cli/Context.hpp"
#include "CommandUsage.hpp"

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <ranges>
#include <stdexcept>

namespace vh::test::integration::cmd {

static bool isFieldMatch(const std::string& field, const std::vector<std::string>& aliases) {
    return std::ranges::any_of(aliases, [&field](const std::string& t){ return t == field; });
}

struct UserAliases {
    std::vector<std::string> nameAliases;
    std::vector<std::string> emailAliases;
    std::vector<std::string> roleAliases;
    std::vector<std::string> linuxUidAliases;

    explicit UserAliases(const std::shared_ptr<cli::Context>& ctx) {
        const auto cmd = ctx->getCommand(EntityType::USER, "update");
        if (!cmd) throw std::runtime_error("EntityFactory: command usage not found for user update");
        for (const auto& opt : cmd->optional) {
            if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "name" || t == "username"; })) {
                nameAliases.insert(nameAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            } else if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "email"; })) {
                emailAliases.insert(emailAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            } else if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "role" || t == "role_id"; })) {
                roleAliases.insert(roleAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            } else if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t.contains("uid"); })) {
                linuxUidAliases.insert(linuxUidAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            }
        }
    }

    [[nodiscard]] bool isName (const std::string& field) const { return isFieldMatch(field, nameAliases); }
    [[nodiscard]] bool isEmail (const std::string& field) const { return isFieldMatch(field, emailAliases); }
    [[nodiscard]] bool isRole (const std::string& field) const { return isFieldMatch(field, roleAliases); }
    [[nodiscard]] bool isLinuxUID (const std::string& field) const { return isFieldMatch(field, linuxUidAliases); }
};

struct GroupAliases {
    std::vector<std::string> nameAliases, descAliases, gidAliases;

    explicit GroupAliases(const std::shared_ptr<cli::Context>& ctx) {
        const auto cmd = ctx->getCommand(EntityType::GROUP, "update");
        if (!cmd) throw std::runtime_error("EntityFactory: command usage not found for group update");
        for (const auto& opt : cmd->optional) {
            if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "name" || t == "group_name"; })) {
                nameAliases.insert(nameAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            } else if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "description" || t == "desc"; })) {
                descAliases.insert(descAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            } else if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t.contains("gid"); })) {
                gidAliases.insert(gidAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            }
        }
    }

    [[nodiscard]] bool isName (const std::string& field) const { return isFieldMatch(field, nameAliases); }
    [[nodiscard]] bool isDescription (const std::string& field) const { return isFieldMatch(field, descAliases); }
    [[nodiscard]] bool isLinuxGID (const std::string& field) const { return isFieldMatch(field, gidAliases); }
};

struct UserRoleAliases {
    std::vector<std::string> nameAliases, permAliases, descAliases;

    explicit UserRoleAliases(const std::shared_ptr<cli::Context>& ctx) {
        const auto cmd = ctx->getCommand(EntityType::ADMIN_ROLE, "update");
        if (!cmd) throw std::runtime_error("EntityFactory: command usage not found for user role update");
        for (const auto& opt : cmd->optional) {
            if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "name" || t == "role_name"; })) {
                nameAliases.insert(nameAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            } else if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "description" || t == "desc"; })) {
                descAliases.insert(descAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            }
        }

        permAliases.emplace_back("permissions");
        permAliases.emplace_back("perms");
    }

    [[nodiscard]] bool isName (const std::string& field) const { return isFieldMatch(field, nameAliases); }
    [[nodiscard]] bool isPermissions (const std::string& field) const { return isFieldMatch(field, permAliases); }
    [[nodiscard]] bool isDescription (const std::string& field) const { return isFieldMatch(field, descAliases); }
};

struct VaultRoleAliases {
    std::vector<std::string> nameAliases, permAliases, descAliases;

    explicit VaultRoleAliases(const std::shared_ptr<cli::Context>& ctx) {
        const auto cmd = ctx->getCommand(EntityType::VAULT_ROLE, "update");
        if (!cmd) throw std::runtime_error("EntityFactory: command usage not found for vault role update");
        for (const auto& opt : cmd->optional) {
            if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "name" || t == "role_name"; })) {
                nameAliases.insert(nameAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            } else if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "description" || t == "desc"; })) {
                descAliases.insert(descAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            }
        }

        permAliases.emplace_back("permissions");
        permAliases.emplace_back("perms");
    }

    [[nodiscard]] bool isName (const std::string& field) const { return isFieldMatch(field, nameAliases); }
    [[nodiscard]] bool isPermissions (const std::string& field) const { return isFieldMatch(field, permAliases); }
    [[nodiscard]] bool isDescription (const std::string& field) const { return isFieldMatch(field, descAliases); }
};

struct VaultAliases {
    std::vector<std::string> nameAliases, descAliases, quotaAliases, ownerAliases, conflictPolicyAliases, intervalAliases;

    explicit VaultAliases(const std::shared_ptr<cli::Context>& ctx) {
        const auto cmd = ctx->getCommand(EntityType::VAULT, "update");
        if (!cmd) throw std::runtime_error("EntityFactory: command usage not found for vault update");
        for (const auto& opt : cmd->optional) {
            if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "name" || t == "vault_name"; })) {
                nameAliases.insert(nameAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            } else if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "description" || t == "desc"; })) {
                descAliases.insert(descAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            } else if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "quota"; })) {
                quotaAliases.insert(quotaAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            } else if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "owner" || t == "owner_id"; })) {
                ownerAliases.insert(ownerAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            } else if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "on_sync_conflict" || t == "conflict_policy" || t == "conflict"; })) {
                conflictPolicyAliases.insert(conflictPolicyAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            } else if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "interval" || t == "sync_interval"; })) {
                intervalAliases.insert(intervalAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            }
        }
    }

    [[nodiscard]] bool isName (const std::string& field) const { return isFieldMatch(field, nameAliases); }
    [[nodiscard]] bool isDescription (const std::string& field) const { return isFieldMatch(field, descAliases); }
    [[nodiscard]] bool isQuota (const std::string& field) const { return isFieldMatch(field, quotaAliases); }
    [[nodiscard]] bool isOwner (const std::string& field) const { return isFieldMatch(field, ownerAliases); }
    [[nodiscard]] bool isConflictPolicy (const std::string& field) const { return isFieldMatch(field, conflictPolicyAliases); }
    [[nodiscard]] bool isInterval (const std::string& field) const { return isFieldMatch(field, intervalAliases); }
};

struct S3VaultAliases : VaultAliases {
    std::vector<std::string> apiKeyAliases, syncStrategyAliases;

    explicit S3VaultAliases(const std::shared_ptr<cli::Context>& ctx) : VaultAliases(ctx) {
        const auto cmd = ctx->getCommand(EntityType::VAULT, "update");
        if (!cmd) throw std::runtime_error("EntityFactory: command usage not found for vault update");
        for (const auto& opt : cmd->optional) {
            if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "api_key" || t == "api_key_id"; })) {
                apiKeyAliases.insert(apiKeyAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            } else if (std::ranges::any_of(opt.option_tokens, [](const std::string& t){ return t == "sync_strategy" || t == "sync"; })) {
                syncStrategyAliases.insert(syncStrategyAliases.end(), opt.option_tokens.begin(), opt.option_tokens.end());
            }
        }
    }

    [[nodiscard]] bool isApiKey (const std::string& field) const { return isFieldMatch(field, apiKeyAliases); }
    [[nodiscard]] bool isSyncStrategy (const std::string& field) const { return isFieldMatch(field, syncStrategyAliases); }
};

}
