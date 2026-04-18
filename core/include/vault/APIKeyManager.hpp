#pragma once

#include "crypto/secrets/TPMKeyProvider.hpp"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace vh::vault {

namespace model { struct APIKey; }

class APIKeyManager {
public:
    APIKeyManager();

    void initAPIKeys();

    unsigned int addAPIKey(std::shared_ptr<model::APIKey>& key);
    void removeAPIKey(unsigned int keyId, unsigned int userId);

    [[nodiscard]] std::vector<std::shared_ptr<model::APIKey>> listAPIKeys() const;
    [[nodiscard]] std::vector<std::shared_ptr<model::APIKey>> listUserAPIKeys(unsigned int userId) const;
    [[nodiscard]] std::shared_ptr<model::APIKey> getAPIKey(unsigned int keyId, unsigned int userId) const;

private:
    mutable std::mutex apiKeysMutex_;
    std::unordered_map<unsigned int, std::shared_ptr<model::APIKey>> apiKeys_;
    std::unique_ptr<crypto::secrets::TPMKeyProvider> tpmKeyProvider_;
};

}
