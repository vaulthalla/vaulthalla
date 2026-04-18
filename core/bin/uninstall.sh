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

# === Make sure FUSE is fully unmounted ===
./bin/teardown/unmount_fuse.sh

# === 1) Stop and disable systemd service (if exists) ===
./bin/teardown/uninstall_systemd.sh

# === 2) Remove binaries ===
./bin/teardown/uninstall_binaries.sh

# === 3) Remove runtime and config dirs ===
./bin/teardown/uninstall_dirs.sh

# === 4) Remove system user ===
./bin/teardown/uninstall_users.sh

# === 5) Drop PostgreSQL DB and user ===
./bin/teardown/uninstall_db.sh

# === 6) Uninstall Deps ===
./bin/teardown/uninstall_deps.sh

# === Done ===
echo
echo "✅ Vaulthalla has been uninstalled."
echo "🧙‍♂️ If this was a test install, may your next deployment rise stronger from these ashes."
