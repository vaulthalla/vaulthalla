#!/usr/bin/env bash
set -euo pipefail

source ./bin/lib/dev_mode.sh

echo "🗡️  Initiating Vaulthalla uninstallation sequence..."

vh_assert_dev_mode_consistency

DEV_MODE=false
if vh_is_dev_mode; then
    DEV_MODE=true
fi

echo "🔍 Build mode: ${VH_BUILD_MODE:-unset}"
echo "🔍 Dev mode active: $DEV_MODE"

# === 0) Unmount FUSE ===
./bin/tests/unmount_fuse.sh

# === 1) Remove binaries ===
./bin/teardown/uninstall_binaries.sh

# === 2) Remove runtime and config dirs ===
./bin/tests/uninstall_dirs.sh

# === 3) Remove system user ===
./bin/teardown/uninstall_users.sh

# === 4) Drop PostgreSQL DB and user ===
./bin/tests/uninstall_db.sh

# === Done ===
echo
echo "✅ Vaulthalla has been uninstalled."
echo "🧙‍♂️ If this was a test install, may your next deployment rise stronger from these ashes."
