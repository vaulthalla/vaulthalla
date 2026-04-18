#pragma once

#include <string>

namespace vh::vault::terms {

static constexpr std::string_view ENABLE_UPSTREAM_ENCRYPTION_WAIVER = R"(

⚠️  You are enabling encryption on a vault that already contains files in the upstream S3 bucket.

This operation will:
- Encrypt and overwrite all existing files in the bucket
- Permanently change the format and accessibility of those files
- Require your encryption key for any future access

⚠️  If any other service currently reads from this bucket, it will immediately break.

❗ You are solely responsible for:
- Backing up the unencrypted originals
- Storing your vault encryption keys securely
- Acknowledging that Vaulthalla will not assist in key recovery or data restoration

By proceeding, you acknowledge that:
- You understand and accept the risks
- You have made proper backups if needed
- You are fully responsible for downstream consequences

Please type "I ACCEPT" to confirm:

)";

static constexpr std::string_view DISABLE_UPSTREAM_ENCRYPTION_WAIVER = R"(

⚠️ You are disabling upstream encryption for this vault.

Going forward, files uploaded to the upstream S3 bucket will:
- Be stored in plaintext
- Be accessible without decryption
- No longer carry encryption metadata

This may:
- Weaken your data security posture
- Violate compliance expectations
- Reduce auditability and chain-of-trust guarantees

Vaulthalla will continue to encrypt data locally. You are solely responsible for securing your upstream bucket.

By proceeding, you acknowledge that:
- You understand and accept the risks
- You are fully responsible for data security in the upstream bucket

Please type "I ACCEPT" to confirm:

)";

}
