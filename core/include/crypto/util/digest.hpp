#pragma once

#include <openssl/evp.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace vh::crypto::util {

// A streaming message digest over OpenSSL's EVP interface (the MD5_* / SHA256_* functions are deprecated in
// OpenSSL 3). `md` is e.g. EVP_md5() or EVP_sha256().
class EvpDigest {
public:
    explicit EvpDigest(const EVP_MD* md);

    void update(const void* data, std::size_t size);

    // The digest of everything passed to update(). Call once.
    [[nodiscard]] std::vector<unsigned char> finish();

    [[nodiscard]] static std::vector<unsigned char> of(const EVP_MD* md, const void* data, std::size_t size);

private:
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx_;
};

}
