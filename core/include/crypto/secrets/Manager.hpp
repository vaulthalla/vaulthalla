#pragma once

#include "crypto/secrets/TPMKeyProvider.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace vh::crypto::secrets {

class Manager {
public:
    Manager();

    std::string jwtSecret() const;
    void setJWTSecret(const std::string& secret) const;
    [[nodiscard]] std::optional<std::string> getSecret(const std::string& key) const;
    void setSecret(const std::string& key, const std::string& value) const;
    [[nodiscard]] bool hasSecret(const std::string& key) const;

private:
    mutable std::mutex mutex_;
    std::unique_ptr<TPMKeyProvider> tpmKeyProvider_;

    std::string getOrInitSecret(const std::string& key) const;
    std::string decryptStoredSecret(const std::vector<uint8_t>& value, const std::vector<uint8_t>& iv) const;
    void setEncryptedValue(const std::string& key, const std::string& value) const;
};

}
