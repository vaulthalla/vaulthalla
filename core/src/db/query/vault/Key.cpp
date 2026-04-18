#include "db/query/vault/Key.hpp"
#include "db/Transactions.hpp"
#include "vault/model/Key.hpp"
#include "db/encoding/bytea.hpp"

using namespace vh::db::encoding;

namespace vh::db::query::vault {

unsigned int Key::addVaultKey(const KeyPtr& key) {
    return Transactions::exec("Key::addVaultKey", [&](pqxx::work& txn) {
        pqxx::params p{key->vaultId, to_hex_bytea(key->encrypted_key), to_hex_bytea(key->iv)};
        const auto res = txn.exec(pqxx::prepped{"insert_vault_key"}, p);
        if (res.empty()) throw std::runtime_error("Failed to add vault key: no result returned");
        return res.one_field().as<unsigned int>();
    });
}

void Key::deleteVaultKey(unsigned int vaultId) {
    Transactions::exec("Key::deleteVaultKey", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"delete_vault_key"}, vaultId);
    });
}

void Key::updateVaultKey(const KeyPtr& key) {
    Transactions::exec("Key::updateVaultKey", [&](pqxx::work& txn) {
        pqxx::params p{key->vaultId, to_hex_bytea(key->encrypted_key), to_hex_bytea(key->iv)};
        txn.exec(pqxx::prepped{"update_vault_key"}, p);
    });
}

Key::KeyPtr Key::getVaultKey(unsigned int vaultId) {
    return Transactions::exec("Key::getVaultKey", [&](pqxx::work& txn) -> KeyPtr {
        const auto res = txn.exec(pqxx::prepped{"get_vault_key"}, vaultId);
        if (res.empty()) return nullptr;
        return std::make_shared<K>(res[0]);
    });
}

unsigned int Key::rotateVaultKey(const KeyPtr& newKey) {
    return Transactions::exec("Key::rotateVaultKey", [&](pqxx::work& txn) {
        pqxx::params p{newKey->vaultId, to_hex_bytea(newKey->encrypted_key), to_hex_bytea(newKey->iv)};
        const auto res = txn.exec(pqxx::prepped{"rotate_vault_key"}, p);
        if (res.empty()) throw std::runtime_error("Failed to rotate vault key: no result returned");
        return res.one_field().as<unsigned int>();
    });
}

void Key::markKeyRotationFinished(unsigned int vaultId) {
    Transactions::exec("Key::markKeyRotationFinished", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"mark_vault_key_rotation_finished"}, vaultId);
    });
}

bool Key::keyRotationInProgress(unsigned int vaultId) {
    return Transactions::exec("Key::keyRotationInProgress", [&](pqxx::work& txn) {
        return txn.exec(pqxx::prepped{"vault_key_rotation_in_progress"}, vaultId).one_field().as<bool>();
    });
}

Key::KeyPtr Key::getRotationInProgressOldKey(unsigned int vaultId) {
    return Transactions::exec("Key::getRotationInProgressOldKey", [&](pqxx::work& txn) -> KeyPtr {
        const auto res = txn.exec(pqxx::prepped{"get_rotation_old_vault_key"}, vaultId);
        if (res.empty()) throw std::runtime_error("No old vault key found for rotation in progress");
        return std::make_shared<K>(res[0]);
    });
}

}
