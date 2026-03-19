#include "db/query/vault/APIKey.hpp"
#include "db/Transactions.hpp"
#include "vault/model/APIKey.hpp"
#include "db/encoding/bytea.hpp"
#include "identities/User.hpp"
#include "db/query/identities/helpers.hpp"

using namespace vh::db::query::vault;
using namespace vh::db::model;
using namespace vh::db::encoding;

unsigned int APIKey::upsertAPIKey(const APIKeyPtr& key) {
    return Transactions::exec("APIKey::addAPIKey", [&](pqxx::work& txn) {
        pqxx::params p{
            key->user_id,
            key->name,
            to_string(key->provider),
            key->access_key,
            to_hex_bytea(key->encrypted_secret_access_key),
            to_hex_bytea(key->iv),
            key->region,
            key->endpoint
        };
        return txn.exec(pqxx::prepped{"upsert_api_key"}, p).one_field().as<unsigned int>();
    });
}

void APIKey::removeAPIKey(const unsigned int keyId) {
    Transactions::exec("APIKey::removeAPIKey", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"remove_api_key"}, keyId);
    });
}

std::vector<APIKey::APIKeyPtr> APIKey::listAPIKeys(const unsigned int userId, const ListQueryParams& params) {
    return Transactions::exec("APIKey::listAPIKeys", [&](pqxx::work& txn) {
        const auto sql = appendPaginationAndFilter(
            "SELECT * FROM api_keys WHERE user_id = " + txn.quote(userId),
            params, "id", "name"
        );
        return vh::vault::model::api_keys_from_pq_res(txn.exec(sql));
    });
}

std::vector<APIKey::APIKeyPtr> APIKey::listAPIKeys(const ListQueryParams& params) {
    return Transactions::exec("APIKey::listAPIKeys", [&](pqxx::work& txn) {
        const auto sql = appendPaginationAndFilter(
            "SELECT * FROM api_keys",
            params, "id", "name"
        );
        return vh::vault::model::api_keys_from_pq_res(txn.exec(sql));
    });
}

APIKey::APIKeyPtr APIKey::getAPIKey(const unsigned int keyId) {
    return Transactions::exec("APIKey::getAPIKey", [&](pqxx::work& txn) -> APIKeyPtr {
        const auto res = txn.exec(pqxx::prepped{"get_api_key"}, keyId);
        if (res.empty()) return nullptr;
        return std::make_shared<AK>(res.one_row());
    });
}

APIKey::APIKeyPtr APIKey::getAPIKey(const std::string& keyName) {
    if (keyName.empty()) return nullptr;

    return Transactions::exec("APIKey::getAPIKeyByName", [&](pqxx::work& txn) -> APIKeyPtr {
        const auto res = txn.exec(pqxx::prepped{"get_api_key_by_name"}, keyName);
        if (res.empty()) return nullptr;
        return std::make_shared<AK>(res.one_row());
    });
}

std::shared_ptr<vh::identities::User> APIKey::getAPIKeyOwner(unsigned int keyId) {
    return Transactions::exec("APIKey::getAPIKeyOwner", [&](pqxx::work& txn) -> std::shared_ptr<vh::identities::User> {
        const auto res = txn.exec(pqxx::prepped{"get_api_key_owner"}, keyId);
        if (res.empty()) return nullptr;
        return identities::hydrateUser(txn, res.one_row());
    });
}
