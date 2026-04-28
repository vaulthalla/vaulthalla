#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN_DIR="$REPO_ROOT/bin"

source "$BIN_DIR/lib/dev_mode.sh"

echo "🗡️  Initiating Vaulthalla uninstallation sequence..."

vh_assert_dev_mode_consistency

DEV_MODE=false
vh_is_dev_mode && DEV_MODE=true

echo "🔍 Build mode: ${VH_BUILD_MODE:-unset}"
echo "🔍 Dev mode active: $DEV_MODE"

REMOVE_DB=false
REMOVE_USERS=false
PURGE_DEPS=false

if [[ "$DEV_MODE" == true ]]; then
  REMOVE_DB=true
  REMOVE_USERS=true
fi

usage() {
  cat <<EOF
Usage: $0 [options]

Options:
  --remove-db        Drop PostgreSQL database and user
  --remove-users     Remove system user/group
  --purge-deps       Uninstall apt dependencies installed for Vaulthalla
  -h, --help         Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --remove-db)
      REMOVE_DB=true
      shift
      ;;
    --remove-users)
      REMOVE_USERS=true
      shift
      ;;
    --purge-deps)
      PURGE_DEPS=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      usage
      exit 1
      ;;
  esac
done

# 1) Make sure FUSE is fully unmounted
"$BIN_DIR/teardown/unmount_fuse.sh"

# 2) Stop and remove systemd services
"$BIN_DIR/teardown/uninstall_systemd.sh"

# 3) Remove installed binaries/artifacts
"$BIN_DIR/teardown/uninstall_binaries.sh"
"$REPO_ROOT/web/bin/teardown_web.sh"

# 4) Remove runtime/config dirs
"$BIN_DIR/teardown/uninstall_dirs.sh"

# 5) Optionally remove DB
if [[ "$REMOVE_DB" == true ]]; then
  "$BIN_DIR/teardown/uninstall_db.sh"
fi

# 6) Optionally remove system user/group
if [[ "$REMOVE_USERS" == true ]]; then
  "$BIN_DIR/teardown/uninstall_users.sh"
fi

# 7) Optionally purge dependencies
if [[ "$PURGE_DEPS" == true ]]; then
  "$BIN_DIR/teardown/uninstall_deps.sh"
fi

echo
echo "✅ Vaulthalla has been uninstalled."
echo "🧙‍♂️ If this was a test install, may your next deployment rise stronger from these ashes."
