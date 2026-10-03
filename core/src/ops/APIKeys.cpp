#include "ops/APIKeys.hpp"

#include "db/query/identities/User.hpp"
#include "db/query/vault/APIKey.hpp"
#include "identities/User.hpp"
#include "rbac/resolver/admin/all.hpp"
#include "runtime/Deps.hpp"
#include "storage/s3/Controller.hpp"
#include "vault/APIKeyManager.hpp"

#include <paths.h>

#include <algorithm>
#include <utility>

namespace vh::ops::api_keys {

namespace {

using KeyPerm = rbac::permission::admin::keys::APIPermissions;

bool canOnKey(const Actor& actor, const KeyPerm permission, const unsigned int keyId) {
    return rbac::resolver::Admin::has<KeyPerm>({.user = actor, .permission = permission, .api_key_id = keyId});
}

std::string describe(const Ref& ref) {
    if (const auto* id = std::get_if<unsigned int>(&ref)) return std::to_string(*id);
    return std::get<std::string>(ref);
}

APIKeyPtr requireKey(const Ref& ref) {
    APIKeyPtr key;
    if (const auto* id = std::get_if<unsigned int>(&ref)) {
        if (*id == 0) throw Invalid("API key ID must be a positive integer");
        key = db::query::vault::APIKey::getAPIKey(*id);
    } else {
        key = db::query::vault::APIKey::getAPIKey(std::get<std::string>(ref));
    }
    if (!key) throw NotFound("API key not found: " + describe(ref));
    return key;
}

std::shared_ptr<identities::User> requireOwner(const Actor& actor, const std::optional<Ref>& ref) {
    if (!ref) return actor;
    std::shared_ptr<identities::User> user;
    if (const auto* id = std::get_if<unsigned int>(&*ref)) user = db::query::identities::User::getUserById(*id);
    else user = db::query::identities::User::getUserByName(std::get<std::string>(*ref));
    if (!user) throw NotFound("owner not found: " + describe(*ref));
    return user;
}

}

APIKeyPtr create(const Actor& actor, const Create& req) {
    requireActor(actor);
    const auto owner = requireOwner(actor, req.owner);
    if (!rbac::resolver::Admin::has<KeyPerm>({.user = actor, .permission = KeyPerm::Create, .target_user_id = owner->id}))
        throw Denied("you do not have permission to create API keys for this owner");

    std::vector<std::string> missing;
    if (req.name.empty()) missing.emplace_back("name");
    if (req.access_key.empty()) missing.emplace_back("access key");
    if (req.secret_access_key.empty()) missing.emplace_back("secret key");
    if (req.endpoint.empty()) missing.emplace_back("endpoint");
    if (!missing.empty()) {
        std::string msg = "missing required";
        for (const auto& m : missing) msg += " " + m + (m == missing.back() ? "" : ",");
        throw Invalid(msg);
    }

    auto key = std::make_shared<vault::model::APIKey>(owner->id, req.name, req.provider, req.access_key,
                                                      req.secret_access_key, req.region.empty() ? "auto" : req.region,
                                                      req.endpoint);
    if (!paths::testMode) {
        const auto [valid, errors] = storage::s3::Controller(key, "").validateAPICredentials();
        if (!valid) throw Invalid("API key validation failed:\n" + errors);
    }

    key->id = runtime::Deps::get().apiKeyManager->addAPIKey(key);
    return requireKey(key->id);
}

APIKeyPtr remove(const Actor& actor, const Ref& ref) {
    requireActor(actor);
    auto key = requireKey(ref);
    if (!canOnKey(actor, KeyPerm::Remove, key->id)) throw Denied("you do not have permission to remove this API key");
    runtime::Deps::get().apiKeyManager->removeAPIKey(key->id);
    return key;
}

APIKeyPtr get(const Actor& actor, const Ref& ref) {
    requireActor(actor);
    auto key = requireKey(ref);
    if (!canOnKey(actor, KeyPerm::View, key->id)) throw Denied("you do not have permission to view this API key");
    return key;
}

std::vector<APIKeyPtr> list(const Actor& actor, db::model::ListQueryParams params) {
    requireActor(actor);
    const auto& perms = actor->apiKeysPerms();
    auto keys = perms.self.canView() && !(perms.admin.canView() || perms.user.canView())
        ? runtime::Deps::get().apiKeyManager->listUserAPIKeys(actor->id)
        : runtime::Deps::get().apiKeyManager->listAPIKeys();
    std::erase_if(keys, [&](const APIKeyPtr& key) { return !key || !canOnKey(actor, KeyPerm::View, key->id); });

    // The visibility filter runs per key, so --filter/--sort/--limit/--page apply here, after it.
    if (params.filter)
        std::erase_if(keys, [&](const APIKeyPtr& key) { return key->name.find(*params.filter) == std::string::npos; });
    if (params.sort && *params.sort != "name" && *params.sort != "id")
        throw Invalid("API keys can be sorted by 'name' or 'id'");
    const bool byName = params.sort && *params.sort == "name";
    const bool desc = params.direction && *params.direction == db::model::SortDirection::DESC;
    std::ranges::sort(keys, [&](const APIKeyPtr& a, const APIKeyPtr& b) {
        const bool less = byName ? a->name < b->name : a->id < b->id;
        const bool greater = byName ? b->name < a->name : b->id < a->id;
        return desc ? greater : less;
    });
    if (params.limit) {
        const auto page = params.page.value_or(1);
        const auto start = std::min<std::size_t>(keys.size(), (page > 0 ? page - 1 : 0) * *params.limit);
        const auto end = std::min<std::size_t>(keys.size(), start + *params.limit);
        keys = std::vector<APIKeyPtr>(keys.begin() + static_cast<std::ptrdiff_t>(start),
                                      keys.begin() + static_cast<std::ptrdiff_t>(end));
    }
    return keys;
}

}
