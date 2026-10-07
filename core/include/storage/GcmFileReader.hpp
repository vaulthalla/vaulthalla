#pragma once

#include "crypto/IntegrityRegistry.hpp"
#include "crypto/SecretKey.hpp"
#include "storage/PlaintextReader.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vh::storage {

// Positioned reader over one AES-256-GCM v1 message stored in a local file (vault file ciphertext, or a derived
// artifact after its header). Reads pread the ciphertext and decrypt only the requested region with the CTR
// keystream; no plaintext reaches the disk. Integrity: the whole message is authenticated once per exact
// generation through crypto::IntegrityRegistry (optimistic: in the background, aborting readers on failure;
// strict: before the first byte is released).
class GcmFileReader final : public PlaintextReader {
public:
    struct Params {
        std::filesystem::path path;
        uint64_t dataOffset{};              // bytes before the GCM body (artifact header)
        uint64_t plaintextSize{};
        crypto::SecretKeyPtr key;            // null ⇒ unencrypted legacy bytes, passed through
        std::array<uint8_t, 12> iv{};
        std::vector<uint8_t> aad;
        // When set: the first expectedHeader.size() bytes of the file, as the caller parsed them (IV, key version).
        // Checked on this reader's own descriptor, so a file replaced between the caller's parse and this open fails.
        std::vector<uint8_t> expectedHeader;
        Generation generation;
        std::string integrityDomain;
        bool strict{false};
        crypto::IntegrityRegistry::CurrentCheck stillCurrent;  // optional: distinguishes replacement from tamper
    };

    explicit GcmFileReader(Params params);
    ~GcmFileReader() override;

    [[nodiscard]] uint64_t size() const override { return params_.plaintextSize; }
    std::size_t read(uint64_t offset, std::span<uint8_t> out) override;
    [[nodiscard]] const Generation& generation() const override { return params_.generation; }
    [[nodiscard]] std::vector<uint8_t> readAllAuthenticated(uint64_t maxBytes) override;

    [[nodiscard]] crypto::IntegrityState integrityState() const;

private:
    void ensureTicket();
    void throwIfFailed() const;
    std::size_t preadFully(uint64_t fileOffset, std::span<uint8_t> out) const;

    Params params_;
    int fd_{-1};
    crypto::IntegrityKey integrityKey_;
    std::shared_ptr<crypto::IntegrityTicket> ticket_;
};

// Process-wide defaults for the FromConfig policies (set from preview.media.* at startup).
void setDefaultIntegrityPolicy(IntegrityPolicy policy);
void setDefaultRemotePolicy(RemoteFetchPolicy policy);
[[nodiscard]] IntegrityPolicy resolveIntegrityPolicy(IntegrityPolicy policy);
[[nodiscard]] RemoteFetchPolicy resolveRemotePolicy(RemoteFetchPolicy policy);

}
