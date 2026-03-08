#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "CommandUsage.hpp"

#include "db/query/vault/Key.hpp"

#include "log/Registry.hpp"
#include "sync/Controller.hpp"

#include "storage/Manager.hpp"
#include "storage/Engine.hpp"

#include "vault/EncryptionManager.hpp"
#include "crypto/encryptors/GPG.hpp"

#include "vault/model/Vault.hpp"
#include "vault/model/APIKey.hpp"
#include "identities/model/User.hpp"

#include "config/Registry.hpp"
#include "CommandUsage.hpp"
#include "usages.hpp"

#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <fstream>

using namespace vh;
using namespace vh::protocols::shell;
using namespace vh::protocols::shell::commands::vault;
using namespace vh::vault::model;
using namespace vh::identities::model;
using namespace vh::storage;
using namespace vh::config;
using namespace vh::crypto;


static CommandResult handle_key_encrypt_and_response(const CommandCall& call,
                                                     const nlohmann::json& output,
                                                     const std::shared_ptr<CommandUsage>& usage) {
    const auto outputOpt = optVal(call, usage->resolveOptional("output")->option_tokens);

    if (const auto recipientOpt = optVal(call, usage->resolveOptional("recipient")->option_tokens)) {
        if (recipientOpt->empty()) return invalid("vault keys export: --recipient requires a value");
        if (!outputOpt) return invalid("vault keys export: --recipient requires --output to specify the output file");

        try {
            encryptors::GPG::encryptToFile(output, *recipientOpt, *outputOpt);
            return ok("Vault key successfully encrypted and saved to " + *outputOpt);
        } catch (const std::exception& e) {
            return invalid("vault keys export: failed to encrypt vault key: " + std::string(e.what()));
        }
    }

    if (outputOpt) {
        log::Registry::audit()->warn(
            "[shell::handle_key_encrypt_and_response] No recipient specified, saving unencrypted key(s) to " + *
            outputOpt);
        try {
            std::ofstream outFile(*outputOpt);
            if (!outFile) return invalid("vault keys export: failed to open output file " + *outputOpt);
            outFile << output.dump(4);
            outFile.close();
            return {0, "Vault key(s) successfully saved to " + *outputOpt,
                    "\nWARNING: No recipient specified, key(s) are unencrypted.\n"
                    "\nConsider using --recipient with a GPG fingerprint to encrypt the key(s) before saving."};
        } catch (const std::exception& e) {
            return invalid("vault keys export: failed to write to output file: " + std::string(e.what()));
        }
    }

    log::Registry::audit()->warn(
        "[shell::handle_key_encrypt_and_response] No recipient specified, returning unencrypted key(s)");
    return {0, output.dump(4),
            "\nWARNING: No recipient specified, key(s) are unencrypted.\n"
            "\nConsider using --recipient with a GPG fingerprint along with --output\nto securely encrypt the key(s) to an output file."};
}


static CommandResult export_one_key(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage) {
    constexpr const auto* ERR = "vault keys export";

    const auto vaultArg = call.positionals[0];

    const auto engLkp = resolveEngine(call, vaultArg, usage, ERR);
    if (!engLkp || !engLkp.ptr) return invalid(engLkp.error);
    const auto engine = engLkp.ptr;

    const auto context = fmt::format("User: {} -> {}", call.user->name, __func__);
    const auto& key = engine->encryptionManager->get_key(context);
    const auto vaultKey = db::query::vault::Key::getVaultKey(engine->vault->id);

    const auto out = generate_json_key_object(engine->vault, key, vaultKey, call.user->name);
    return handle_key_encrypt_and_response(call, out, usage);
}


static CommandResult export_all_keys(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage) {
    const auto engines = runtime::Deps::get().storageManager->getEngines();
    if (engines.empty()) return invalid("vault keys export: no vaults found");

    nlohmann::json out = nlohmann::json::array();

    const auto context = fmt::format("User: {} -> {}", call.user->name, __func__);
    for (const auto& engine : engines) {
        const auto& key = engine->encryptionManager->get_key(context);
        const auto vaultKey = db::query::vault::Key::getVaultKey(engine->vault->id);
        out.push_back(generate_json_key_object(engine->vault, key, vaultKey, call.user->name));
    }

    return handle_key_encrypt_and_response(call, out, usage);
}


static CommandResult handle_export_vault_keys(const CommandCall& call) {
    if (!call.user->isSuperAdmin()) {
        if (!call.user->canManageEncryptionKeys()) return invalid(
            "vault keys export: only super admins can export vault keys");

        log::Registry::audit()->warn(
            "\n[shell::handle_export_vault_keys] User {} called to export vault keys without super admin privileges\n"
            "WARNING: It is extremely dangerous to assign this permission to non super-admin users, proceed at your own risk.\n",
            call.user->name);
    }

    const auto usage = resolveUsage({"vault", "keys", "export"});
    validatePositionals(call, usage);

    if (call.positionals[0] == "all") return export_all_keys(call, usage);
    return export_one_key(call, usage);
}


static CommandResult handle_inspect_vault_key(const CommandCall& call) {
    constexpr const auto* ERR = "vault keys inspect";

    if (!call.user->isSuperAdmin()) {
        if (!call.user->canManageEncryptionKeys()) return invalid(
            "vault keys inspect: only super admins can inspect vault keys");

        log::Registry::audit()->warn(
            "\n[shell::handle_inspect_vault_key] User {} called to inspect vault keys without super admin privileges\n"
            "WARNING: It is extremely dangerous to assign this permission to non super-admin users, proceed at your own risk.\n",
            call.user->name);
    }

    const auto usage = resolveUsage({"vault", "keys", "inspect"});
    validatePositionals(call, usage);

    const auto engLkp = resolveEngine(call, call.positionals[0], usage, ERR);
    if (!engLkp || !engLkp.ptr) return invalid(engLkp.error);
    const auto engine = engLkp.ptr;

    const auto vaultKey = db::query::vault::Key::getVaultKey(engine->vault->id);

    return ok(generate_json_key_info_object(engine->vault, vaultKey, call.user->name).dump(4));
}


static CommandResult handle_rotate_vault_keys(const CommandCall& call) {
    constexpr const auto* ERR = "vault keys rotate";

    if (!call.user->isSuperAdmin()) {
        if (!call.user->canManageEncryptionKeys()) return invalid(
            "vault keys rotate: only super admins or users with manage encryption keys or vaults can rotate vault keys");

        log::Registry::audit()->warn(
            "\n[shell::handle_rotate_vault_keys] User {} called to rotate vault keys without super admin privileges\n"
            "WARNING: It is extremely dangerous to assign this permission to non super-admin users, proceed at your own risk.\n",
            call.user->name);
    }

    const auto usage = resolveUsage({"vault", "keys", "rotate"});
    validatePositionals(call, usage);

    const auto syncNow = hasFlag(call, usage->resolveFlag("now")->aliases);
    const auto rotateKey = [&syncNow](const std::shared_ptr<Engine>& engine) {
        engine->encryptionManager->prepare_key_rotation();
        if (syncNow) runtime::Deps::get().syncController->runNow(engine->vault->id);
    };

    const auto vaultArg = call.positionals[0];

    if (vaultArg == "all") {
        for (const auto& engine : runtime::Deps::get().storageManager->getEngines())
            rotateKey(engine);

        return ok("Vault keys for all vaults have been rotated successfully.\n"
            "If you have --now flag set, the sync will be triggered immediately.");
    }

    const auto engLkp = resolveEngine(call, vaultArg, usage, ERR);
    if (!engLkp || !engLkp.ptr) return invalid(engLkp.error);
    const auto engine = engLkp.ptr;

    rotateKey(engine);

    return ok("Vault key for '" + engine->vault->name + "' (ID: " + std::to_string(engine->vault->id) +
              ") has been rotated successfully.\n"
              "If you have --now flag set, the sync will be triggered immediately.");
}

static bool isVaultKeysMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"vault", "keys", cmd}, input);
}

CommandResult commands::vault::handle_vault_keys(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    const auto [subcommand, subcall] = descend(call);

    if (isVaultKeysMatch("export", subcommand)) return handle_export_vault_keys(subcall);
    if (isVaultKeysMatch("rotate", subcommand)) return handle_rotate_vault_keys(subcall);
    if (isVaultKeysMatch("inspect", subcommand)) return handle_inspect_vault_key(subcall);

    return invalid("vault keys: unknown subcommand '" + std::string(subcommand) + "'. Use: export | rotate | inspect");
}
