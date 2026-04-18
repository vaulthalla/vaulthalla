#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CORE_DIR="$ROOT_DIR/core"
WEB_DIR="$ROOT_DIR/web"

source "$ROOT_DIR/bin/lib/dev_mode.sh"

echo "🗡️  Initiating Vaulthalla installation sequence..."

vh_assert_dev_mode_consistency

CORE_ARGS=()
WEB_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--dev)
      export VH_BUILD_MODE=debug
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

"$ROOT_DIR/bin/setup/install_deps.sh"
"$ROOT_DIR/bin/setup/install_users.sh"
"$ROOT_DIR/bin/setup/install_dirs.sh"
"$CORE_DIR/bin/install.sh" "${CORE_ARGS[@]}"
# "$WEB_DIR/bin/install.sh" "${WEB_ARGS[@]}"
"$ROOT_DIR/bin/setup/install_db.sh"
"$ROOT_DIR/bin/setup/install_systemd.sh"
