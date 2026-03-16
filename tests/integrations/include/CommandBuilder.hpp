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

namespace vh::protocols::shell {
class CommandUsage;
class UsageManager;
}

namespace vh::test::cli {

template <typename T = void> class CommandBuilder {
public:
    virtual ~CommandBuilder() = default;

    explicit CommandBuilder(const std::shared_ptr<protocols::shell::UsageManager>& usage,
                            const std::shared_ptr<CLITestContext>& ctx, const std::string& rootTopLevelAlias)
        : ctx_(ctx) {
        if (!usage) throw std::runtime_error("CommandBuilder: usage manager is null");
        const auto cmd = usage->resolve(rootTopLevelAlias);
        if (!cmd) throw std::runtime_error("CommandBuilder: command usage not found for root: " + rootTopLevelAlias);
        root_ = cmd;
    }

    virtual std::string create(const std::shared_ptr<T>& entity) = 0;

    virtual std::string update(const std::shared_ptr<T>& entity) = 0;

    virtual std::string remove(const std::shared_ptr<T>& entity) = 0;

    virtual std::string info(const std::shared_ptr<T>& entity) = 0;

    virtual std::string list() = 0;

protected:
    std::shared_ptr<protocols::shell::CommandUsage> root_;
    std::shared_ptr<CLITestContext> ctx_;

    virtual std::string updateAndResolveVar(const std::shared_ptr<T>& entity, const std::string& field) = 0;
};

class UserCommandBuilder final : CommandBuilder<identities::User> {
public:
    explicit UserCommandBuilder(const std::shared_ptr<protocols::shell::UsageManager>& usage,
                                const std::shared_ptr<CLITestContext>& ctx);

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

class VaultCommandBuilder final : CommandBuilder<vault::model::Vault> {
public:
    explicit VaultCommandBuilder(const std::shared_ptr<protocols::shell::UsageManager>& usage,
                                 const std::shared_ptr<CLITestContext>& ctx);

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

class GroupCommandBuilder final : CommandBuilder<identities::Group> {
public:
    explicit GroupCommandBuilder(const std::shared_ptr<protocols::shell::UsageManager>& usage,
                                 const std::shared_ptr<CLITestContext>& ctx);

    ~GroupCommandBuilder() override = default;

    std::string create(const std::shared_ptr<identities::Group>& entity) override;

    std::string update(const std::shared_ptr<identities::Group>& entity) override;

    std::string remove(const std::shared_ptr<identities::Group>& entity) override;

    std::string info(const std::shared_ptr<identities::Group>& entity) override;

    std::string list() override;

    std::string addUser(const std::shared_ptr<identities::Group>& entity, const std::shared_ptr<identities::User>& user) const;

    std::string removeUser(const std::shared_ptr<identities::Group>& entity, const std::shared_ptr<identities::User>& user) const;

protected:
    std::string updateAndResolveVar(const std::shared_ptr<identities::Group>& entity, const std::string& field) override;

private:
    GroupAliases groupAliases_;
};

class UserRoleCommandBuilder final : CommandBuilder<rbac::role::Admin> {
public:
    explicit UserRoleCommandBuilder(const std::shared_ptr<protocols::shell::UsageManager>& usage,
                                    const std::shared_ptr<CLITestContext>& ctx);

    ~UserRoleCommandBuilder() override = default;

    std::string create(const std::shared_ptr<rbac::role::Admin>& entity) override;

    std::string update(const std::shared_ptr<rbac::role::Admin>& entity) override;

    std::string remove(const std::shared_ptr<rbac::role::Admin>& entity) override;

    std::string info(const std::shared_ptr<rbac::role::Admin>& entity) override;

    std::string list() override;

protected:
    std::string updateAndResolveVar(const std::shared_ptr<rbac::role::Admin>& entity, const std::string& field) override;

private:
    UserRoleAliases userRoleAliases_;
};

class VaultRoleCommandBuilder final : CommandBuilder<rbac::role::Vault> {
public:
    explicit VaultRoleCommandBuilder(const std::shared_ptr<protocols::shell::UsageManager>& usage,
                                     const std::shared_ptr<CLITestContext>& ctx);

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