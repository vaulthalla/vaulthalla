#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "identities/User.hpp"
#include "crypto/secrets/TPMKeyProvider.hpp"
#include "crypto/secrets/Manager.hpp"
#include "fs/ops/file.hpp"
#include <nlohmann/json.hpp>
#include "crypto/encryptors/GPG.hpp"
#include "log/Registry.hpp"
#include "runtime/Deps.hpp"
#include "usage/include/UsageManager.hpp"
#include "CommandUsage.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"

#include <fstream>
#include <paths.h>

using namespace vh;
using namespace vh::protocols::shell;
using namespace vh::identities;
using namespace vh::fs::ops;

static std::vector<uint8_t> trimSecret(const std::vector<uint8_t>& secret) {
    auto start = secret.begin();
    while (start != secret.end() && std::isspace(*start)) ++start;
    auto end = secret.end();
    do { --end; } while (end != start && std::isspace(*end));
    return {start, end + 1};
}

static CommandResult handle_secrets_set(const CommandCall& call) {
    const auto usage = resolveUsage({"secrets", "set"});
    validatePositionals(call, usage);

    const auto secretArg = call.positionals[0];
    const auto fileArg = call.positionals[1];

    if (!std::filesystem::exists(fileArg)) return invalid("secrets set: file does not exist: " + fileArg);

    const auto secret = trimSecret(readFileToVector(fileArg));

    if (secretArg == "db-password") {
        vh::crypto::secrets::TPMKeyProvider tpm(vh::paths::testMode ? "test_psql" : "psql");
        tpm.init();
        tpm.updateMasterKey(secret);
        return ok("Successfully updated database password secret (sealed with TPM)");
    }

    if (secretArg == "jwt-secret") {
        runtime::Deps::get().secretsManager->setJWTSecret({secret.begin(), secret.end()});
        return ok("Successfully updated JWT secret");
    }

    return invalid("secrets set: unknown secret '" + std::string(secretArg) + "'. Valid secrets are: db-password, jwt-secret");
}

static CommandResult handle_secret_encrypt_and_response(const CommandCall& call,
                                                     const nlohmann::json& output,
                                                     const std::shared_ptr<CommandUsage>& usage) {
    const auto outputOpt = optVal(call, usage->resolveOptional("output")->option_tokens);

    if (const auto recipientOpt = optVal(call, usage->resolveOptional("recipient")->option_tokens)) {
        if (recipientOpt->empty()) return invalid("secrets export: --recipient requires a value");
        if (!outputOpt) return invalid("secrets export: --recipient requires --output to specify the output file");

        try {
            vh::crypto::encryptors::GPG::encryptToFile(output, *recipientOpt, *outputOpt);
            return ok("Secret successfully encrypted and saved to " + *outputOpt);
        } catch (const std::exception& e) {
            return invalid("secrets export: failed to encrypt secret: " + std::string(e.what()));
        }
    }

    if (outputOpt) {
        log::Registry::audit()->warn(
            "[shell::handle_secret_encrypt_and_response] No recipient specified, saving unencrypted key(s) to " + *outputOpt);
        try {
            std::ofstream outFile(*outputOpt);
            if (!outFile) return invalid("secrets export: failed to open output file " + *outputOpt);
            outFile << output.dump(4);
            outFile.close();
            return {0, "secret(s) successfully saved to " + *outputOpt,
                    "\nWARNING: No recipient specified, key(s) are unencrypted.\n"
                    "\nConsider using --recipient with a GPG fingerprint to encrypt the key(s) before saving."};
        } catch (const std::exception& e) {
            return invalid("secrets export: failed to write to output file: " + std::string(e.what()));
        }
    }

    log::Registry::audit()->warn(
        "[shell::handle_secret_encrypt_and_response] No recipient specified, returning unencrypted key(s)");
    return {0, output.dump(4),
            "\nWARNING: No recipient specified, key(s) are unencrypted.\n"
            "\nConsider using --recipient with a GPG fingerprint along with --output\nto securely encrypt the key(s) to an output file."};
}

static nlohmann::json generateSecretOutput(const std::string& name, const std::vector<uint8_t>& secret) {
    return {
            {"name", name},
            {"secret", std::string(secret.begin(), secret.end())}
    };
}

static nlohmann::json generateSecretOutput(const std::string& name, const std::string& secret) {
    return {
            {"name", name},
            {"secret", secret}
    };
}

static nlohmann::json getDBPassword() {
    vh::crypto::secrets::TPMKeyProvider tpm(vh::paths::testMode ? "test_psql" : "psql");
    tpm.init();
    return generateSecretOutput("db-password", tpm.getMasterKey());
}

static nlohmann::json getJWTSecret() {
    return generateSecretOutput("jwt-secret", runtime::Deps::get().secretsManager->jwtSecret());
}

static CommandResult handle_secrets_export(const CommandCall& call) {
    const auto usage = resolveUsage({"secrets", "export"});
    validatePositionals(call, usage);

    const auto secretArg = call.positionals[0];

    nlohmann::json out = nlohmann::json::array();

    if (secretArg == "db-password" || secretArg == "all") out.push_back(getDBPassword());
    if (secretArg == "jwt-secret" || secretArg == "all") out.push_back(getJWTSecret());

    if (out.empty()) return invalid("secrets export: unknown secret '" + std::string(secretArg) + "'. Valid secrets are: db-password, jwt-secret, all");
    return handle_secret_encrypt_and_response(call, out, usage);
}

static bool isSecretsMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"secrets", cmd}, input);
}

static CommandResult handle_secrets(const CommandCall& call) {
    if (!call.user->encryptionKeysPerms().canExport())
        return invalid("secrets export: insufficient permissions to export secrets. Requires encryption keys export permission.");

    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);

    if (isSecretsMatch("set", sub)) return handle_secrets_set(subcall);
    if (isSecretsMatch("export", sub)) return handle_secrets_export(subcall);

    return invalid(call.constructFullArgs(), "Unknown secrets subcommand: '" + std::string(sub) + "'");
}

void commands::registerSecretsCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("secrets"), handle_secrets);
}
