#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
CORE_DIR="$ROOT_DIR/core"

source "$ROOT_DIR/bin/lib/dev_mode.sh"

echo "🗡️  Initiating Vaulthalla integration test environment setup..."

vh_assert_dev_mode_consistency

RUN_TEST=false
CLEAN_BUILD=false

usage() {
  cat <<EOF
Usage: $0 [options]

Options:
  --run         Build and run integration tests
  --clean       Clean build first
  -h, --help    Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --run)
      RUN_TEST=true
      shift
      ;;
    --clean)
      CLEAN_BUILD=true
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

"$ROOT_DIR/bin/setup/install_deps.sh"
"$ROOT_DIR/bin/setup/install_users.sh"
"$ROOT_DIR/bin/tests/install_dirs.sh"
"$ROOT_DIR/bin/tests/install_db.sh"

if [[ "$RUN_TEST" == true ]]; then
  CORE_ARGS=()
  [[ "$CLEAN_BUILD" == true ]] && CORE_ARGS+=("--clean")
  [[ "$RUN_TEST" == true ]] && CORE_ARGS+=("--run")
  "$CORE_DIR/bin/tests/install.sh" "${CORE_ARGS[@]}"
fi
