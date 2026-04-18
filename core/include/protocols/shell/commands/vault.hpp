#pragma once

#include "protocols/shell/types.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "helpers.hpp"

#include <memory>
#include <optional>
#include <regex>
#include <string>

namespace vh::protocols::shell {
    class Router;
    class CommandUsage;
}

namespace vh::identities {
    struct User;
}

namespace vh::rbac::role {
    struct Vault;
    enum class OverrideOpt;
}

namespace vh::vault::model {
    struct Vault;
    struct S3Vault;
    struct APIKey;
    enum class VaultType;
}

namespace vh::sync::model {
    struct Policy;
    struct Waiver;
}

namespace vh::storage {
    struct Engine;
}

namespace vh::protocols::shell::commands::vault {
    struct WaiverContext {
        const CommandCall &call;
        std::shared_ptr<vh::vault::model::Vault> vault;
        bool isUpdate = false;
    };

    struct WaiverResult {
        bool okToProceed;
        std::shared_ptr<sync::model::Waiver> waiver;
    };

    // router.cpp
    void registerCommands(const std::shared_ptr<Router> &r);

    // waiver.cpp
    WaiverResult handle_encryption_waiver(const WaiverContext &ctx);

    // create.cpp
    CommandResult handle_vault_create(const CommandCall &call);

    // lifecycle.cpp
    CommandResult handle_vault_update(const CommandCall &call);

    CommandResult handle_vault_delete(const CommandCall &call);

    // listinfo.cpp
    CommandResult handle_vault_info(const CommandCall &call);

    CommandResult handle_vaults_list(const CommandCall &call);

    // role.cpp
    CommandResult handle_vault_role(const CommandCall &call);
    CommandResult handle_vault_role_override(const CommandCall& call);

    // keys.cpp
    CommandResult handle_vault_keys(const CommandCall &call);

    // sync.cpp
    CommandResult handle_sync(const CommandCall &call);

    // helpers.cpp
    std::optional<unsigned int> parsePositiveUint(const std::string &s, const char *errLabel, std::string &errOut);

    std::shared_ptr<identities::User> resolveOwner(const CommandCall &call, const std::shared_ptr<CommandUsage> &usage);

    Lookup<identities::User> resolveOwnerRequired(const CommandCall &call, const std::shared_ptr<CommandUsage> &usage,
                                                  const std::string &errPrefix);

    Lookup<vh::vault::model::Vault> resolveVault(const CommandCall &call, const std::string &vaultArg,
                                                 const std::shared_ptr<CommandUsage> &usage,
                                                 const std::string &errPrefix);

    Lookup<storage::Engine> resolveEngine(const CommandCall &call, const std::string &vaultArg,
                                          const std::shared_ptr<CommandUsage> &usage, const std::string &errPrefix);

    std::optional<std::string> checkOverridePermissions(const CommandCall &call,
                                                        const std::shared_ptr<vh::vault::model::Vault> &vault,
                                                        const std::string &errPrefix);

    Lookup<vh::rbac::role::Vault> resolveVRole(const std::string &roleArg,
                                           const std::shared_ptr<vh::vault::model::Vault> &vault,
                                           const Subject *subjectOrNull,
                                           const std::string &errPrefix);

    PatternParse parseGlobPatternOpt(const CommandCall &call, bool required, const std::string &errPrefix);

    EnableParse parseEnableDisableOpt(const CommandCall &call, const std::string &errPrefix);

    EffectParse parseEffectChangeOpt(const CommandCall &call, const std::string &errPrefix);

    std::unique_ptr<vh::vault::model::VaultType> parseVaultType(const CommandCall &call);

    void assignDescIfAvailable(const CommandCall &call, const std::shared_ptr<CommandUsage> &usage,
                               const std::shared_ptr<vh::vault::model::Vault> &vault);

    void assignQuotaIfAvailable(const CommandCall &call, const std::shared_ptr<CommandUsage> &usage,
                                const std::shared_ptr<vh::vault::model::Vault> &vault);

    void assignOwnerIfAvailable(const CommandCall &call, const std::shared_ptr<CommandUsage> &usage,
                                const std::shared_ptr<vh::vault::model::Vault> &vault);

    void parseSync(const CommandCall &call, const std::shared_ptr<CommandUsage> &usage,
                   const std::shared_ptr<vh::vault::model::Vault> &vault,
                   const std::shared_ptr<sync::model::Policy> &sync);

    void parseS3API(const CommandCall &call, const std::shared_ptr<CommandUsage> &usage,
                    const std::shared_ptr<vh::vault::model::Vault> &vault, bool required = false);
}