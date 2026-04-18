#pragma once

#include <memory>

namespace vh::vault::model { struct Key; }

namespace vh::db::query::vault {

class Key {
    using K = vh::vault::model::Key;
    using KeyPtr = std::shared_ptr<K>;

public:
    [[nodiscard]] static unsigned int addVaultKey(const KeyPtr& key);
    static void deleteVaultKey(unsigned int vaultId);
    static void updateVaultKey(const KeyPtr& key);
    static KeyPtr getVaultKey(unsigned int vaultId);
    [[nodiscard]] static unsigned int rotateVaultKey(const KeyPtr& newKey);
    static void markKeyRotationFinished(unsigned int vaultId);
    [[nodiscard]] static bool keyRotationInProgress(unsigned int vaultId);
    [[nodiscard]] static KeyPtr getRotationInProgressOldKey(unsigned int vaultId);
};

}
