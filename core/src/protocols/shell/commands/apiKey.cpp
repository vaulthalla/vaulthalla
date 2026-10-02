#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/Router.hpp"
#include "vault/model/APIKey.hpp"
#include "identities/User.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "protocols/shell/util/runOp.hpp"
#include "ops/APIKeys.hpp"
#include "runtime/Deps.hpp"
#include "usage/include/UsageManager.hpp"
#include "CommandUsage.hpp"

#include <nlohmann/json.hpp>



using namespace vh;
using namespace vh::protocols::shell;
using namespace vh::vault::model;


// CLI provider spellings (the web sends display names; both map to the same S3Provider).
static S3Provider s3_provider_from_shell_input(const std::string &str) {
    if (str == "aws") return S3Provider::AWS;
    if (str == "cloudflare-r2") return S3Provider::CloudflareR2;
    if (str == "wasabi") return S3Provider::Wasabi;
    if (str == "backblaze-b2") return S3Provider::BackblazeB2;
    if (str == "digitalocean") return S3Provider::DigitalOcean;
    if (str == "minio") return S3Provider::MinIO;
    if (str == "ceph") return S3Provider::Ceph;
    if (str == "storj") return S3Provider::Storj;
    if (str == "other") return S3Provider::Other;
    throw ops::Invalid("invalid provider '" + str + "' (aws, cloudflare-r2, wasabi, backblaze-b2, digitalocean, minio, ceph, storj, other)");
}

// CLI syntax only: a numeric positional names a key by id, anything else by name.
static ops::api_keys::Ref apiKeyCliRef(const std::string &nameOrId) {
    if (const auto id = parseUInt(nameOrId)) return *id;
    return nameOrId;
}

static CommandResult handleListAPIKeys(const CommandCall &call) {
    const auto usage = resolveUsage({"api-key", "list"});
    validatePositionals(call, usage);
    const bool json = hasFlag(call, "json");

    return runOp("api-key list", [&] { return ops::api_keys::list(call.user, parseListQuery(call)); },
        [&](const auto &keys) {
            if (!json) return to_string(keys);
            auto out = nlohmann::json(keys).dump(4);
            out.push_back('\n');
            return out;
        });
}

static CommandResult handleCreateAPIKey(const CommandCall &call) {
    const auto usage = resolveUsage({"api-key", "create"});
    validatePositionals(call, usage);

    return runOp("api-key create", [&] {
        const auto provider = optVal(call, usage->resolveRequired("provider")->option_tokens);
        return ops::api_keys::create(call.user, {
            .name = call.positionals[0],
            .provider = s3_provider_from_shell_input(provider.value_or("")),
            .access_key = optVal(call, usage->resolveRequired("access")->option_tokens).value_or(""),
            .secret_access_key = optVal(call, usage->resolveRequired("secret")->option_tokens).value_or(""),
            .endpoint = optVal(call, usage->resolveRequired("endpoint")->option_tokens).value_or(""),
            .region = optVal(call, usage->resolveOptional("region")->option_tokens).value_or("auto")
        });
    }, [](const auto &key) { return "Successfully created API key!\n" + to_string(key); });
}

static CommandResult handleDeleteAPIKey(const CommandCall &call) {
    const auto usage = resolveUsage({"api-key", "delete"});
    validatePositionals(call, usage);
    return runOp("api-key delete", [&] { return ops::api_keys::remove(call.user, apiKeyCliRef(call.positionals[0])); },
        [](const auto &key) { return "API key deleted successfully: " + std::to_string(key->id) + "\n"; });
}

static CommandResult handleAPIKeyInfo(const CommandCall &call) {
    const auto usage = resolveUsage({"api-key", "info"});
    validatePositionals(call, usage);
    return runOp("api-key info", [&] { return ops::api_keys::get(call.user, apiKeyCliRef(call.positionals[0])); },
        [](const auto &key) { return to_string(key); });
}

static bool isAPIKeyMatch(const std::string &cmd, const std::string_view input) {
    return isCommandMatch({"api-key", cmd}, input);
}

static CommandResult handle_key(const CommandCall &call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);

    if (isAPIKeyMatch("list", sub)) return handleListAPIKeys(subcall);
    if (isAPIKeyMatch("create", sub)) return handleCreateAPIKey(subcall);
    if (isAPIKeyMatch("delete", sub)) return handleDeleteAPIKey(subcall);
    if (isAPIKeyMatch("info", sub)) return handleAPIKeyInfo(subcall);

    return invalid(call.constructFullArgs(), "Unknown api-key subcommand: '" + std::string(sub) + "'");
}

void commands::registerAPIKeyCommands(const std::shared_ptr<Router> &r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("api-key"), handle_key);
}
