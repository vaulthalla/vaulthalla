#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CORE_DIR="$ROOT_DIR/core"

source "$ROOT_DIR/bin/lib/dev_mode.sh"

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
"$CORE_DIR/bin/teardown/unmount_fuse.sh"

# 2) Stop and remove systemd services
"$ROOT_DIR/bin/teardown/uninstall_systemd.sh"

# 3) Remove installed binaries/artifacts
"$ROOT_DIR/bin/teardown/uninstall_binaries.sh"

# 4) Remove runtime/config dirs
"$ROOT_DIR/bin/teardown/uninstall_dirs.sh"

# 5) Optionally remove DB
if [[ "$REMOVE_DB" == true ]]; then
  "$ROOT_DIR/bin/teardown/uninstall_db.sh"
fi

# 6) Optionally remove system user/group
if [[ "$REMOVE_USERS" == true ]]; then
  "$ROOT_DIR/bin/teardown/uninstall_users.sh"
fi

# 7) Optionally purge dependencies
if [[ "$PURGE_DEPS" == true ]]; then
  "$ROOT_DIR/bin/teardown/uninstall_deps.sh"
fi

echo
echo "✅ Vaulthalla has been uninstalled."
echo "🧙‍♂️ If this was a test install, may your next deployment rise stronger from these ashes."
