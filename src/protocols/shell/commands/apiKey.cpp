#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/Router.hpp"
#include "db/query/vault/APIKey.hpp"
#include "vault/model/APIKey.hpp"
#include "identities/model/User.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "vault/APIKeyManager.hpp"
#include "storage/s3/Controller.hpp"
#include "runtime/Deps.hpp"
#include "usage/include/UsageManager.hpp"
#include "config/Registry.hpp"
#include "CommandUsage.hpp"

#include <paths.h>

using namespace vh;
using namespace vh::protocols::shell;
using namespace vh::vault::model;
using namespace vh::crypto;
using namespace vh::storage;
using namespace vh::config;

static S3Provider s3_provider_from_shell_input(const std::string& str) {
    if (str == "aws") return S3Provider::AWS;
    if (str == "cloudflare-r2") return S3Provider::CloudflareR2;
    if (str == "wasabi") return S3Provider::Wasabi;
    if (str == "backblaze-b2") return S3Provider::BackblazeB2;
    if (str == "digitalocean") return S3Provider::DigitalOcean;
    if (str == "minio") return S3Provider::MinIO;
    if (str == "ceph") return S3Provider::Ceph;
    if (str == "storj") return S3Provider::Storj;
    if (str == "other") return S3Provider::Other;
    throw std::invalid_argument("Invalid provider: " + str);
}

static CommandResult handleListAPIKeys(const CommandCall& call) {
    const auto usage = resolveUsage({"api-key", "list"});
    validatePositionals(call, usage);

    const auto params = parseListQuery(call);

    std::vector<std::shared_ptr<APIKey>> keys;
    if (call.user->canManageAPIKeys()) keys = db::query::vault::APIKey::listAPIKeys(params);
    else keys = db::query::vault::APIKey::listAPIKeys(call.user->id, params);

    const auto jsonFlag = usage->resolveFlag("json");
    if (hasFlag(call, jsonFlag->aliases)) {
        auto out = nlohmann::json(keys).dump(4);
        out.push_back('\n');
        return ok(out);
    }

    return ok(to_string(keys));
}

static CommandResult handleCreateAPIKey(const CommandCall& call) {
    const auto usage = resolveUsage({"api-key", "create"});
    validatePositionals(call, usage);

    const auto name = call.positionals[0];

    const auto accessKeyOpt = optVal(call, usage->resolveRequired("access")->option_tokens);
    const auto secretOpt = optVal(call, usage->resolveRequired("secret")->option_tokens);
    const auto endpointOpt = optVal(call, usage->resolveRequired("endpoint")->option_tokens);
    const auto providerOpt = optVal(call, usage->resolveRequired("provider")->option_tokens);
    const auto regionOpt = optVal(call, usage->resolveOptional("region")->option_tokens);

    std::vector<std::string> errors;
    if (!accessKeyOpt || accessKeyOpt->empty()) errors.emplace_back("Missing required option: --access");
    if (!secretOpt || secretOpt->empty()) errors.emplace_back("Missing required option: --secret");
    if (!endpointOpt || endpointOpt->empty()) errors.emplace_back("Missing required option: --endpoint");
    if (!providerOpt || providerOpt->empty()) errors.emplace_back("Missing required option: --provider");

    if (!errors.empty()) {
        std::string errorMsg = "API key creation failed:\n";
        for (const auto& err : errors) errorMsg += "  - " + err + "\n";
        return invalid(errorMsg);
    }

    auto key = std::make_shared<APIKey>();
    key->user_id = call.user->id;
    key->name = name;
    key->access_key = *accessKeyOpt;
    key->secret_access_key = *secretOpt;
    key->region = regionOpt ? *regionOpt : "auto";
    key->endpoint = *endpointOpt;
    key->provider = s3_provider_from_shell_input(*providerOpt);

    if (!vh::paths::testMode) {
        const auto [valid, validationErrors] = s3::Controller(key, "").validateAPICredentials();
        if (!valid) return invalid("API key validation failed:\n" + validationErrors);
    }

    key->id = runtime::Deps::get().apiKeyManager->addAPIKey(key);

    return ok("Successfully created API key!\n" + to_string(key));
}

static std::shared_ptr<APIKey> resolveAPIKey(const std::string& nameOrId) {
    if (nameOrId.empty()) return nullptr;
    if (const auto& idOpt = parseUInt(nameOrId); idOpt && *idOpt > 0)
        return db::query::vault::APIKey::getAPIKey(*idOpt);
    return db::query::vault::APIKey::getAPIKey(nameOrId);
}

static CommandResult handleDeleteAPIKey(const CommandCall& call) {
    const auto usage = resolveUsage({"api-key", "delete"});
    validatePositionals(call, usage);

    const auto key = resolveAPIKey(call.positionals[0]);
    if (!key) return invalid("API key not found: " + call.positionals[0]);

    if (!call.user->canManageAPIKeys() && key->user_id != call.user->id)
        return invalid("You do not have permission to delete this API key.");

    runtime::Deps::get().apiKeyManager->removeAPIKey(key->id, key->user_id);

    return ok("API key deleted successfully: " + std::to_string(key->id) + "\n");
}

static CommandResult handleAPIKeyInfo(const CommandCall& call) {
    const auto usage = resolveUsage({"api-key", "info"});
    validatePositionals(call, usage);

    const auto key = resolveAPIKey(call.positionals[0]);
    if (!key) return invalid("API key not found: " + call.positionals[0]);

    if (!call.user->canManageAPIKeys() && key->user_id != call.user->id)
        return invalid("You do not have permission to access this API key.");

    return ok(to_string(key));
}

static bool isAPIKeyMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"api-key", cmd}, input);
}

static CommandResult handle_key(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);

    if (isAPIKeyMatch("list", sub)) return handleListAPIKeys(subcall);
    if (isAPIKeyMatch("create", sub)) return handleCreateAPIKey(subcall);
    if (isAPIKeyMatch("delete", sub)) return handleDeleteAPIKey(subcall);
    if (isAPIKeyMatch("info", sub)) return handleAPIKeyInfo(subcall);

    return invalid(call.constructFullArgs(), "Unknown api-key subcommand: '" + std::string(sub) + "'");
}

void commands::registerAPIKeyCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("api-key"), handle_key);
}
