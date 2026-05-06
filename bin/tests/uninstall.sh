#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BIN_DIR="$REPO_ROOT/bin"

source "$BIN_DIR/lib/dev_mode.sh"

echo "🗡️  Initiating Vaulthalla integration test teardown..."

vh_assert_dev_mode_consistency

DEV_MODE=false
vh_is_dev_mode && DEV_MODE=true

echo "🔍 Build mode: ${VH_BUILD_MODE:-unset}"
echo "🔍 Dev mode active: $DEV_MODE"

# === 0) Unmount FUSE test mount(s) ===
"$BIN_DIR/tests/unmount_fuse.sh"

# === 1) Remove integration test runtime/config dirs ===
"$BIN_DIR/tests/uninstall_dirs.sh"

# === 2) Remove integration test DB and user ===
"$BIN_DIR/tests/uninstall_db.sh"

echo
echo "✅ Vaulthalla integration test environment has been removed."
echo "🧙‍♂️ May the next test run rise cleaner than the last."
