#pragma once

#include "identities/User.hpp"
#include "vault/model/Vault.hpp"
#include "identities/Group.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "updateAliases.hpp"
#include "UsageManager.hpp"

#include <memory>
#include <string>
#include <fmt/format.h>

namespace vh::protocols::shell {
class CommandUsage;
class UsageManager;
}

namespace vh::test::integration::cmd {

template <typename T = void> class Builder {
public:
    virtual ~Builder() = default;

    Builder(const std::shared_ptr<protocols::shell::UsageManager>& usage,
                            const std::shared_ptr<cli::Context>& ctx, const std::string& rootTopLevelAlias)
        : ctx_(ctx) {
        if (!usage) throw std::runtime_error("CommandBuilder: usage manager is null");
        const auto cmd = usage->resolve(rootTopLevelAlias);
        if (!cmd) throw std::runtime_error("CommandBuilder: command usage not found for root: " + rootTopLevelAlias);
        root_ = cmd;
    }

    Builder(const std::shared_ptr<protocols::shell::UsageManager>& usage,
                            const std::shared_ptr<cli::Context>& ctx, const std::vector<std::string>& pathToRoot)
        : ctx_(ctx) {
        if (!usage) throw std::runtime_error("CommandBuilder: usage manager is null");
        const auto cmd = usage->resolve(pathToRoot);
        if (!cmd) throw std::runtime_error("CommandBuilder: command usage not found for root: " + fmt::format("{}", fmt::join(pathToRoot, " ")));
        root_ = cmd;
    }

    virtual std::string create(const std::shared_ptr<T>& entity) = 0;

    virtual std::string update(const std::shared_ptr<T>& entity) = 0;

    virtual std::string remove(const std::shared_ptr<T>& entity) = 0;

    virtual std::string info(const std::shared_ptr<T>& entity) = 0;

    virtual std::string list() = 0;

protected:
    std::shared_ptr<protocols::shell::CommandUsage> root_;
    std::shared_ptr<cli::Context> ctx_;

    virtual std::string updateAndResolveVar(const std::shared_ptr<T>& entity, const std::string& field) = 0;
};

class UserCommandBuilder final : Builder<identities::User> {
public:
    UserCommandBuilder(const std::shared_ptr<protocols::shell::UsageManager>& usage,
                                const std::shared_ptr<cli::Context>& ctx);

    ~UserCommandBuilder() override = default;

    std::string create(const std::shared_ptr<identities::User>& entity) override;

    std::string update(const std::shared_ptr<identities::User>& entity) override;

    std::string remove(const std::shared_ptr<identities::User>& entity) override;

    std::string info(const std::shared_ptr<identities::User>& entity) override;

    std::string list() override;

protected:
    std::string updateAndResolveVar(const std::shared_ptr<identities::User>& entity, const std::string& field) override;

private:
    UserAliases userAliases_;
};

class VaultCommandBuilder final : Builder<vault::model::Vault> {
public:
    VaultCommandBuilder(const std::shared_ptr<protocols::shell::UsageManager>& usage,
                                 const std::shared_ptr<cli::Context>& ctx);

    ~VaultCommandBuilder() override = default;

    std::string create(const std::shared_ptr<vault::model::Vault>& v) override;

    std::string update(const std::shared_ptr<vault::model::Vault>& v) override;

    std::string remove(const std::shared_ptr<vault::model::Vault>& v) override;

    std::string info(const std::shared_ptr<vault::model::Vault>& v) override;

    std::string list() override;

    [[nodiscard]] std::string assignVaultRole(const std::shared_ptr<vault::model::Vault>& vault,
                                const std::shared_ptr<rbac::role::Vault>& role,
                                const EntityType& entityType,
                                const std::shared_ptr<void>& entity) const;

    [[nodiscard]] std::string unassignVaultRole(const std::shared_ptr<vault::model::Vault>& vault,
                                  const std::shared_ptr<rbac::role::Vault>& role,
                                  const EntityType& entityType,
                                  const std::shared_ptr<void>& entity) const;

    std::string sync_set(const std::shared_ptr<vault::model::Vault>& v);

    std::string sync_info(const std::shared_ptr<vault::model::Vault>& v);

    std::string sync_trigger(const std::shared_ptr<vault::model::Vault>& v);

    std::string key_export(const std::shared_ptr<vault::model::Vault>& v);

    std::string key_rotate(const std::shared_ptr<vault::model::Vault>& v);

protected:
    std::string updateAndResolveVar(const std::shared_ptr<vault::model::Vault>& entity, const std::string& field) override;

private:
    S3VaultAliases vaultAliases_;

    static std::string vaultRef(const std::shared_ptr<vault::model::Vault>& v, bool& usedName);

    static void emitOwnerIfName(std::ostringstream& oss, const std::shared_ptr<vault::model::Vault>& v, bool usedName);

    // picks "local" or "s3";
    static std::string chooseVaultType();
};

class GroupCommandBuilder final : Builder<identities::Group> {
public:
    GroupCommandBuilder(const std::shared_ptr<protocols::shell::UsageManager>& usage,
                                 const std::shared_ptr<cli::Context>& ctx);

    ~GroupCommandBuilder() override = default;

    std::string create(const std::shared_ptr<identities::Group>& entity) override;

    std::string update(const std::shared_ptr<identities::Group>& entity) override;

    std::string remove(const std::shared_ptr<identities::Group>& entity) override;

    std::string info(const std::shared_ptr<identities::Group>& entity) override;

    std::string list() override;

    [[nodiscard]] std::string addUser(const std::shared_ptr<identities::Group>& entity, const std::shared_ptr<identities::User>& user) const;

    [[nodiscard]] std::string removeUser(const std::shared_ptr<identities::Group>& entity, const std::shared_ptr<identities::User>& user) const;

protected:
    std::string updateAndResolveVar(const std::shared_ptr<identities::Group>& entity, const std::string& field) override;

private:
    GroupAliases groupAliases_;
};

class AdminRoleCommandBuilder final : Builder<rbac::role::Admin> {
public:
    AdminRoleCommandBuilder(const std::shared_ptr<protocols::shell::UsageManager>& usage,
                                    const std::shared_ptr<cli::Context>& ctx);

    ~AdminRoleCommandBuilder() override = default;

    std::string create(const std::shared_ptr<rbac::role::Admin>& entity) override;

    std::string update(const std::shared_ptr<rbac::role::Admin>& entity) override;

    std::string remove(const std::shared_ptr<rbac::role::Admin>& entity) override;

    std::string info(const std::shared_ptr<rbac::role::Admin>& entity) override;

    std::string list() override;

protected:
    std::string updateAndResolveVar(const std::shared_ptr<rbac::role::Admin>& entity, const std::string& field) override;

private:
    UserRoleAliases adminRoleAliases_;
};

class VaultRoleCommandBuilder final : Builder<rbac::role::Vault> {
public:
    VaultRoleCommandBuilder(const std::shared_ptr<protocols::shell::UsageManager>& usage,
                                     const std::shared_ptr<cli::Context>& ctx);

    ~VaultRoleCommandBuilder() override = default;

    std::string create(const std::shared_ptr<rbac::role::Vault>& entity) override;

    std::string update(const std::shared_ptr<rbac::role::Vault>& entity) override;

    std::string remove(const std::shared_ptr<rbac::role::Vault>& entity) override;

    std::string info(const std::shared_ptr<rbac::role::Vault>& entity) override;

    std::string list() override;

protected:
    std::string updateAndResolveVar(const std::shared_ptr<rbac::role::Vault>& entity, const std::string& field) override;

private:
    VaultRoleAliases vaultRoleAliases_;
};

}