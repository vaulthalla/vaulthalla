#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
CORE_DIR="$ROOT_DIR/core"

source "$ROOT_DIR/bin/lib/dev_mode.sh"

echo "🗡️  Initiating Vaulthalla integration test teardown..."

vh_assert_dev_mode_consistency

DEV_MODE=false
vh_is_dev_mode && DEV_MODE=true

echo "🔍 Build mode: ${VH_BUILD_MODE:-unset}"
echo "🔍 Dev mode active: $DEV_MODE"

# === 0) Unmount FUSE test mount(s) ===
"$CORE_DIR/bin/tests/unmount_fuse.sh"

# === 1) Remove installed binaries/artifacts ===
"$ROOT_DIR/bin/teardown/uninstall_binaries.sh"

# === 2) Remove integration test runtime/config dirs ===
"$ROOT_DIR/bin/tests/uninstall_dirs.sh"

# === 3) Remove integration test DB and user ===
"$ROOT_DIR/bin/tests/uninstall_db.sh"

# === 4) Remove system user/group ===
"$ROOT_DIR/bin/teardown/uninstall_users.sh"

echo
echo "✅ Vaulthalla integration test environment has been removed."
echo "🧙‍♂️ May the next test run rise cleaner than the last."
