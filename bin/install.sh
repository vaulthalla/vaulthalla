#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN_DIR="$REPO_ROOT/bin"

source "$BIN_DIR/lib/dev_mode.sh"

echo "🗡️  Initiating Vaulthalla installation sequence..."

vh_assert_dev_mode_consistency

CORE_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--dev)
      export VH_BUILD_MODE=dev
      shift
      ;;
    -m|--manpage)
      CORE_ARGS+=("--manpage")
      shift
      ;;
    --clean)
      CORE_ARGS+=("--clean")
      shift
      ;;
    -h|--help)
      echo "Usage: $0 [options]"
      echo "  -d, --dev        Enable dev mode"
      echo "  -m, --manpage    Build/install manpage"
      echo "      --clean      Clean core build dir first"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
  esac
done

vh_assert_dev_mode_consistency

"$BIN_DIR/setup/install_deps.sh"
"$BIN_DIR/setup/install_users.sh"
"$BIN_DIR/setup/install_dirs.sh"
"$BIN_DIR/setup/install_core.sh" "${CORE_ARGS[@]}"
"$REPO_ROOT/web/bin/install_web.sh"
"$BIN_DIR/setup/install_db.sh"
"$BIN_DIR/setup/install_systemd.sh"
"$BIN_DIR/setup/install_dev_nginx.sh"

INITIAL_PASSWORD_FILE="/var/lib/vaulthalla/super_admin_initial_password"
echo
echo "Web console: sign in as 'admin'. There is no default password: a new database gets a generated one, written to"
echo "  ${INITIAL_PASSWORD_FILE} (read it with: sudo cat ${INITIAL_PASSWORD_FILE})."
echo "  Change it with 'vh setup set-super-admin-password' (removes the file), or keep it and delete the file."
