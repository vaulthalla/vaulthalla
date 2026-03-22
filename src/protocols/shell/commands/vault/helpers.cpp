#include "protocols/shell/commands/vault.hpp"
#include "vault/model/Vault.hpp"
#include "vault/model/S3Vault.hpp"
#include "vault/model/APIKey.hpp"
#include "identities/User.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/permission/Override.hpp"
#include "db/query/vault/Vault.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/rbac/role/Vault.hpp"
#include "db/query/rbac/role/vault/Assignments.hpp"
#include "db/query/vault/APIKey.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "storage/Manager.hpp"
#include "CommandUsage.hpp"
#include "sync/model/LocalPolicy.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "db/encoding/interval.hpp"
#include "rbac/resolver/admin/all.hpp"
#include "rbac/resolver/vault/all.hpp"

using namespace vh;

namespace vh::protocols::shell::commands::vault {

std::optional<unsigned int> parsePositiveUint(const std::string& s, const char* errLabel, std::string& errOut) {
    if (const auto v = parseUInt(s)) {
        if (*v <= 0) { errOut = std::string(errLabel) + " must be a positive integer"; return std::nullopt; }
        return static_cast<unsigned int>(*v);
    }
    errOut = std::string(errLabel) + " must be a positive integer";
    return std::nullopt;
}

std::shared_ptr<identities::User> resolveOwner(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage) {
    if (const auto ownerOpt = optVal(call, usage->resolveOptional("owner")->option_tokens)) {
        if (const auto idOpt = parseUInt(*ownerOpt)) {
            if (*idOpt <= 0) throw std::runtime_error("owner must be a positive integer");
            const auto user = db::query::identities::User::getUserById(*idOpt);
            if (!user) throw std::runtime_error("owner id not found: " + *ownerOpt);
            return user;
        }
        const auto user = db::query::identities::User::getUserByName(*ownerOpt);
        if (!user) throw std::runtime_error("owner not found: " + *ownerOpt);
        return user;
    }
    return call.user;
}

Lookup<identities::User> resolveOwnerRequired(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage, const std::string& errPrefix) {
    Lookup<identities::User> out;
    const auto ownerOpt = optVal(call, usage->resolveOptional("owner")->option_tokens);
    if (!ownerOpt || ownerOpt->empty()) {
        out.error = errPrefix + ": when using a vault name, you must specify --owner <id|name>";
        return out;
    }
    if (const auto idOpt = parseUInt(*ownerOpt)) {
        if (*idOpt <= 0) { out.error = errPrefix + ": --owner must be a positive integer"; return out; }
        out.ptr = db::query::identities::User::getUserById(*idOpt);
        if (!out.ptr) out.error = errPrefix + ": owner id not found: " + *ownerOpt;
        return out;
    }
    out.ptr = db::query::identities::User::getUserByName(*ownerOpt);
    if (!out.ptr) out.error = errPrefix + ": owner not found: " + *ownerOpt;
    return out;
}

Lookup<::vh::vault::model::Vault> resolveVault(const CommandCall& call, const std::string& vaultArg, const std::shared_ptr<CommandUsage>& usage, const std::string& errPrefix) {
    Lookup<::vh::vault::model::Vault> out;
    if (const auto idOpt = parseUInt(vaultArg)) {
        if (*idOpt <= 0) { out.error = errPrefix + ": vault ID must be a positive integer"; return out; }
        out.ptr = db::query::vault::Vault::getVault(*idOpt);
        if (!out.ptr) out.error = errPrefix + ": vault with id " + std::to_string(*idOpt) + " not found";
        return out;
    }

    auto ownerLkp = resolveOwnerRequired(call, usage, errPrefix);
    if (!ownerLkp) { out.error = std::move(ownerLkp.error); return out; }
    out.ptr = db::query::vault::Vault::getVault(vaultArg, ownerLkp.ptr->id);
    if (!out.ptr) out.error = errPrefix + ": vault named '" + vaultArg + "' (owner id " + std::to_string(ownerLkp.ptr->id) + ") not found";
    return out;
}

Lookup<storage::Engine> resolveEngine(const CommandCall& call, const std::string& vaultArg, const std::shared_ptr<CommandUsage>& usage, const std::string& errPrefix) {
    Lookup<storage::Engine> out;

    const auto vLkp = resolveVault(call, vaultArg, usage, errPrefix);
    if (!vLkp || !vLkp.ptr) { out.error = vLkp.error; return out; }
    const auto vault = vLkp.ptr;

    out.ptr = runtime::Deps::get().storageManager->getEngine(vault->id);
    if (!out.ptr) out.error = errPrefix + ": no storage engine found for vault '" + vaultArg + "'";
    return out;
}

std::optional<std::string> checkOverridePermissions(const CommandCall& call, const std::shared_ptr<vh::vault::model::Vault>& vault, const std::string& errPrefix) {
    if (vault->owner_id == call.user->id) return std::nullopt;

    using Perm = ::vh::rbac::permission::vault::RolePermissions;
    if (!::vh::rbac::resolver::Vault::has<Perm>({
        .user = call.user,
        .permission = Perm::Assign,
        .vault_id = vault->id
    })) return errPrefix + ": you do not have permission to override roles for this vault";

    return std::nullopt;
}

Lookup<::vh::rbac::role::Vault> resolveVRole(const std::string& roleArg,
                              const std::shared_ptr<vh::vault::model::Vault>& vault,
                              const Subject* subjectOrNull,
                              const std::string& errPrefix) {
    Lookup<::vh::rbac::role::Vault> out;
    if (const auto idOpt = parseUInt(roleArg)) {
        if (*idOpt <= 0) { out.error = errPrefix + ": role ID must be a positive integer"; return out; }
        out.ptr = db::query::rbac::role::Vault::get(*idOpt);
        if (!out.ptr) out.error = errPrefix + ": role with id " + std::to_string(*idOpt) + " not found";
        return out;
    }
    if (!subjectOrNull) {
        out.error = errPrefix + ": non-integer role arg requires a subject (--user/--group) to infer the role";
        return out;
    }
    out.ptr = db::query::rbac::role::vault::Assignments::get(vault->id, subjectOrNull->type, subjectOrNull->id);
    if (!out.ptr) out.error = errPrefix + ": role not found for " + subjectOrNull->type + " id " + std::to_string(subjectOrNull->id);
    return out;
}

PatternParse parsePatternOpt(const CommandCall& call, bool required, const std::string& errPrefix) {
    PatternParse out;
    auto p = optVal(call, "path");
    if (!p) p = optVal(call, "pattern");
    if (!p || p->empty()) {
        if (required) out.error = errPrefix + ": --path/--pattern is required";
        else { out.ok = true; }
        return out;
    }
    out.raw = *p;
    try {
        out.compiled = std::regex(out.raw);
    } catch (const std::regex_error&) {
        out.error = errPrefix + ": invalid regex for --path/--pattern";
        return out;
    }
    out.ok = true;
    return out;
}

EnableParse parseEnableDisableOpt(const CommandCall& call, const std::string& errPrefix) {
    EnableParse out;
    const bool hasEnable  = hasFlag(call, "enable");
    const bool hasDisable = hasFlag(call, "disable");
    if (hasEnable && hasDisable) {
        out.error = errPrefix + ": cannot specify both --enable and --disable";
        return out;
    }
    if (hasEnable)  out.value = true;
    if (hasDisable) out.value = false;
    out.ok = true;
    return out;
}

EffectParse parseEffectChangeOpt(const CommandCall& call, const std::string& errPrefix) {
    EffectParse out;
    const bool allowFlag = hasFlag(call, "allow") || hasFlag(call, "allow-effect");
    const bool denyFlag  = hasFlag(call, "deny")  || hasFlag(call, "deny-effect");
    if (allowFlag && denyFlag) {
        out.error = errPrefix + ": cannot set both --allow and --deny";
        return out;
    }
    if (allowFlag) out.value = ::vh::rbac::permission::OverrideOpt::ALLOW;
    if (denyFlag)  out.value = ::vh::rbac::permission::OverrideOpt::DENY;
    out.ok = true;
    return out;
}

std::unique_ptr<::vh::vault::model::VaultType> parseVaultType(const CommandCall& call) {
    const bool local = hasFlag(call, "local");
    const bool s3    = hasFlag(call, "s3");
    if (local && s3) throw std::runtime_error("--local and --s3 are mutually exclusive");
    if (local) return std::make_unique<::vh::vault::model::VaultType>(::vh::vault::model::VaultType::Local);
    if (s3)    return std::make_unique<::vh::vault::model::VaultType>(::vh::vault::model::VaultType::S3);

    throw std::runtime_error("Vault type not specified: must provide either --local or --s3");
}

void assignDescIfAvailable(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage, const std::shared_ptr<vh::vault::model::Vault>& vault) {
    if (const auto descOpt = optVal(call, usage->resolveOptional("description")->option_tokens))
        vault->description = *descOpt;
}

void assignQuotaIfAvailable(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage, const std::shared_ptr<vh::vault::model::Vault>& vault) {
    if (const auto quotaOpt = optVal(call, usage->resolveOptional("quota")->option_tokens)) {
        if (*quotaOpt == "none" || *quotaOpt == "unlimited") vault->quota = 0;
        else vault->quota = parseSize(*quotaOpt);
    }
}

void assignOwnerIfAvailable(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage, const std::shared_ptr<vh::vault::model::Vault>& vault) {
    if (const auto ownerOpt = optVal(call, usage->resolveOptional("owner")->option_tokens)) {
        Lookup<identities::User> ownerLkp;
        if (const auto idOpt = parseUInt(*ownerOpt)) {
            if (*idOpt <= 0) throw std::runtime_error("vault create: --owner must be a positive integer");
            ownerLkp.ptr = db::query::identities::User::getUserById(*idOpt);
        } else ownerLkp.ptr = db::query::identities::User::getUserByName(*ownerOpt);
        if (!ownerLkp.ptr) throw std::runtime_error("vault create: owner not found: " + *ownerOpt);
        vault->owner_id = ownerLkp.ptr->id;
    }
}

void parseSync(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage, const std::shared_ptr<vh::vault::model::Vault>& vault, const std::shared_ptr<sync::model::Policy>& sync) {
    if (const auto syncIntervalOpt = optVal(call, usage->resolveOptional("interval")->option_tokens))
        sync->interval = db::encoding::parseSyncInterval(*syncIntervalOpt);

    if (vault->type == vh::vault::model::VaultType::Local) {
        if (const auto conflictOpt = optVal(call, usage->resolveGroupOptional("Local Vault Options", "conflict")->option_tokens)) {
            const auto fsync = std::static_pointer_cast<sync::model::LocalPolicy>(sync);
            fsync->conflict_policy = sync::model::fsConflictPolicyFromString(*conflictOpt);
        }
    } else if (vault->type == vh::vault::model::VaultType::S3) {
        if (const auto conflictOpt = optVal(call, usage->resolveGroupOptional("S3 Vault Options", "conflict")->option_tokens)) {
            const auto rsync = std::static_pointer_cast<sync::model::RemotePolicy>(sync);
            rsync->conflict_policy = sync::model::rsConflictPolicyFromString(*conflictOpt);
        }
    }

    if (vault->type == vh::vault::model::VaultType::S3) {
        const auto rsync = std::static_pointer_cast<sync::model::RemotePolicy>(sync);
        if (const auto syncStrategyOpt = optVal(call, usage->resolveGroupOptional("S3 Vault Options", "strategy")->option_tokens))
            rsync->strategy = sync::model::strategyFromString(*syncStrategyOpt);
    }
}

void parseS3API(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage, const std::shared_ptr<vh::vault::model::Vault>& vault, const bool required) {
    if (vault->type == vh::vault::model::VaultType::Local) return;

    const auto s3Vault = std::static_pointer_cast<vh::vault::model::S3Vault>(vault);

    if (const auto apiKeyOpt = optVal(call, usage->resolveGroupOptional("S3 Vault Options", "api-key")->option_tokens)) {
        if (const auto apiKeyId = parseUInt(*apiKeyOpt); db::query::vault::APIKey::getAPIKey(*apiKeyId)) s3Vault->api_key_id = *apiKeyId;
        else {
            const auto apiKey = db::query::vault::APIKey::getAPIKey(*apiKeyOpt);
            if (!apiKey) throw std::runtime_error("API key not found: " + *apiKeyOpt);

            using AKPerm = ::vh::rbac::permission::admin::keys::APIPermissions;
            if (!::vh::rbac::resolver::Admin::has<AKPerm>({
                .user = call.user,
                .permission = AKPerm::Consume,
                .api_key_id = s3Vault->api_key_id
            })) throw std::runtime_error("you do not have permission to use this API key");

            s3Vault->api_key_id = apiKey->id;
        }
    } else if (required) throw std::runtime_error("--api-key is required for S3 vaults");

    if (const auto bucketOpt = optVal(call, usage->resolveGroupOptional("S3 Vault Options", "bucket")->option_tokens)) {
        if (bucketOpt->empty()) throw std::runtime_error("--bucket cannot be empty");
        s3Vault->bucket = *bucketOpt;
    } else if (required) throw std::runtime_error("--bucket is required for S3 vaults");
}

}
