#include "db/query/crypto/Secret.hpp"
#include "db/Transactions.hpp"
#include "crypto/model/Secret.hpp"
#include "db/encoding/bytea.hpp"

using namespace vh::db::query::crypto;
using namespace vh::db::encoding;

using S = vh::crypto::model::Secret;

void Secret::upsertSecret(const std::shared_ptr<S>& secret) {
    Transactions::exec("Secret::upsertSecret", [&](pqxx::work& txn) {
        pqxx::params p{secret->key, to_hex_bytea(secret->value), to_hex_bytea(secret->iv)};
        txn.exec(pqxx::prepped{"upsert_internal_secret"}, p);
    });
}

std::shared_ptr<S> Secret::getSecret(const std::string& key) {
    return Transactions::exec("Secret::getSecret", [&](pqxx::work& txn) -> std::shared_ptr<S> {
        const auto res = txn.exec(pqxx::prepped{"get_internal_secret"}, key);
        if (res.empty()) return nullptr;
        return std::make_shared<S>(res.one_row());
    });
}

bool Secret::secretExists(const std::string& key) {
    return Transactions::exec("Secret::secretExists", [&](pqxx::work& txn) -> bool {
        const auto res = txn.exec(pqxx::prepped{"internal_secret_exists"}, key);
        if (res.empty()) return false;
        return res.one_field().as<bool>();
    });
}
