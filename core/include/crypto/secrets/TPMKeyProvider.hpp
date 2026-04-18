#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace vh::crypto::secrets {

class TPMKeyProvider {
public:
    explicit TPMKeyProvider(const std::string& name = "master");

    void init(const std::optional<std::string>& initSecret = std::nullopt);  // check or generate
    [[nodiscard]] const std::vector<uint8_t>& getMasterKey() const;

    void updateMasterKey(const std::vector<uint8_t>& newMasterKey);

    [[nodiscard]] bool sealedExists() const;

private:
    std::vector<uint8_t> masterKey_;
    std::string sealedBlobPath_, name_;
    std::filesystem::path sealedDir_;

    void generate_and_seal();
    void unseal();
};

} // namespace vh::crypto
