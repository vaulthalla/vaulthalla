#include "crypto/util/digest.hpp"

#include <stdexcept>

namespace vh::crypto::util {

EvpDigest::EvpDigest(const EVP_MD* md) : ctx_(EVP_MD_CTX_new(), &EVP_MD_CTX_free) {
    if (!ctx_) throw std::runtime_error("Failed to allocate a digest context");
    if (EVP_DigestInit_ex(ctx_.get(), md, nullptr) != 1) throw std::runtime_error("Digest initialization failed");
}

void EvpDigest::update(const void* data, const std::size_t size) {
    if (size == 0) return;
    if (EVP_DigestUpdate(ctx_.get(), data, size) != 1) throw std::runtime_error("Digest update failed");
}

std::vector<unsigned char> EvpDigest::finish() {
    std::vector<unsigned char> digest(EVP_MAX_MD_SIZE);
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(ctx_.get(), digest.data(), &length) != 1) throw std::runtime_error("Digest finalization failed");
    digest.resize(length);
    return digest;
}

std::vector<unsigned char> EvpDigest::of(const EVP_MD* md, const void* data, const std::size_t size) {
    EvpDigest digest(md);
    digest.update(data, size);
    return digest.finish();
}

}
