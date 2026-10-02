#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "protocols/shell/util/runOp.hpp"
#include "db/query/vault/APIKey.hpp"
#include "identities/User.hpp"
#include "vault/model/APIKey.hpp"
#include "vault/model/S3Vault.hpp"
#include "vault/model/Vault.hpp"
#include "CommandUsage.hpp"

#include <string>
#include <memory>

namespace vh::protocols::shell::commands::vault {

CommandResult handle_vault_update(const CommandCall& call) {
    const auto usage = resolveUsage({"vault", "update"});
    validatePositionals(call, usage);

    const auto vLkp = resolveVault(call, call.positionals[0], usage, "vault update");
    if (!vLkp || !vLkp.ptr) return invalid(vLkp.error);
    const auto vault = vLkp.ptr;

    ops::vaults::Update req{.id = vault->id};
    try {
        req.description = optVal(call, usage->resolveOptional("description")->option_tokens);
        req.quota = quotaFromOption(call);
        // --owner names the owner a vault *name* is looked up under (so it matches and changes nothing); with an id
        // it reassigns the vault.
        if (optVal(call, usage->resolveOptional("owner")->option_tokens)) req.owner_id = resolveOwner(call, usage)->id;
        req.api_key_id = apiKeyIdFromOption(call);
        req.bucket = optVal(call, "bucket");
        if (const auto tier = optVal(call, std::vector<std::string>{"storage-tier", "storage-class"}))
            req.storage_tier = std::optional<std::string>{*tier};
        req.encrypt_upstream = encryptFromFlags(call);
        req.sync = syncPatchFromOptions(call);

        if (hasFlag(call, "interactive") && vault->type == ::vh::vault::model::VaultType::S3) {
            if (!call.io) return invalid("vault update --interactive: requires an interactive terminal");
            const auto s3 = std::static_pointer_cast<::vh::vault::model::S3Vault>(vault);
            const auto current = s3->storage_tier_id.value_or("provider default");
            req.storage_tier = std::optional<std::string>{call.io->prompt("Storage tier [provider default]:", current)};
        }
    } catch (const ops::Error& e) {
        return invalid("vault update: " + std::string(e.what()));
    }

    return runVaultChange(call, "vault update", [&](const bool accept) {
        auto attempt = req;
        attempt.accept_waiver = accept;
        return ops::vaults::update(call.user, attempt);
    }, [](const auto& v) { return "Successfully updated vault!\n" + to_string(v); });
}

CommandResult handle_vault_delete(const CommandCall& call) {
    const auto usage = resolveUsage({"vault", "delete"});
    validatePositionals(call, usage);

    const auto vLkp = resolveVault(call, call.positionals[0], usage, "vault delete");
    if (!vLkp || !vLkp.ptr) return invalid(vLkp.error);

    return runOp("vault delete", [&] { return ops::vaults::remove(call.user, vLkp.ptr->id); },
        [](const auto& v) { return "Successfully deleted vault '" + v->name + "' (ID: " + std::to_string(v->id) + ")\n"; });
}

}
