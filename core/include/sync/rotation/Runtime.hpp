#pragma once

#include "sync/rotation/Rotation.hpp"
#include "storage/Fwd.hpp"

#include <memory>

namespace vh::sync::rotation {

// The daemon's seams for the rotation protocol: the vault's EncryptionManager, compare-and-set on the files table,
// the fs cache (so FUSE decrypts with the committed IV), FUSE working copies (busy files are deferred) and, for
// cloud vaults, the cloud engine (objects carry their own IV/version as metadata).
[[nodiscard]] Deps runtimeDeps(const std::shared_ptr<storage::Engine>& engine);

// Resolves every `*.vh-rotate` sidecar under the vault's backing tree against the files table. Run at the start of
// every rotation pass and once per vault per daemon start.
RecoveryReport recoverVault(const std::shared_ptr<storage::Engine>& engine, const Deps& deps);

// Cloud vaults that encrypt upstream: remote-index entries still recording an older key version for an object that
// rotation re-encrypted (same content hash as its files row, row on keyVersion) are updated to the row's IV and
// version, and the manifest is republished. Without this a later index-only download would copy the stale IV back
// into the files row. Throws when the manifest could not be published.
void reconcileRemoteIndex(const std::shared_ptr<storage::Engine>& engine, unsigned int keyVersion);

}
