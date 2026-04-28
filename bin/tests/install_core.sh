#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CORE_DIR="$REPO_ROOT/core"
BIN_DIR="$REPO_ROOT/bin"

source "$BIN_DIR/lib/dev_mode.sh"

echo "🏗️  Preparing Vaulthalla core integration tests..."

vh_assert_dev_mode_consistency

RUN_TEST=false
CLEAN_BUILD=false

usage() {
  cat <<EOF
Usage: $0 [options]

Options:
  --run         Run integration tests after build/install
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

MESON_ARGS=(
  "-Dbuildtype=debug"
  "-Dintegration_tests=true"
  "-Db_sanitize=address,undefined"
)

BUILD_DIR="$REPO_ROOT/build"

if [[ "$CLEAN_BUILD" == true && -d "$BUILD_DIR" ]]; then
  echo "🧹 Cleaning previous build artifacts..."
  rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

meson setup "$BUILD_DIR" "$REPO_ROOT" "${MESON_ARGS[@]}" --reconfigure
meson compile -C "$BUILD_DIR"
sudo meson install -C "$BUILD_DIR"
sudo ldconfig

if [[ "$RUN_TEST" == true ]]; then
  TEST_BIN="$BUILD_DIR/core/vh_integration_tests"
  if [[ ! -x "$TEST_BIN" ]]; then
    TEST_BIN="$BUILD_DIR/vh_integration_tests"
  fi
  sudo bash --rcfile "$REPO_ROOT/deploy/vaulthalla.env" -i -c "$TEST_BIN"
fi
