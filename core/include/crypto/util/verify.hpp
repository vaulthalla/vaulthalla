#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace vh::crypto::util {

// Whether a ciphertext file (AES-256-GCM body followed by its 16-byte tag, no AAD) authenticates under key and iv.
// Streams the file in 64 KiB chunks; the decrypted chunks only pass through a scratch buffer that is wiped, nothing
// is written anywhere. Returns false when the tag does not verify or the file is shorter than a tag. Throws on bad
// key/IV sizes and on I/O errors (open, short read), so a caller never mistakes "could not read" for "does not
// authenticate".
[[nodiscard]] bool aes256_gcm_file_authenticates(const std::filesystem::path& ciphertextPath,
                                                 const std::vector<uint8_t>& key,
                                                 const std::vector<uint8_t>& iv);

}
