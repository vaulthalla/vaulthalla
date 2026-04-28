#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CORE_DIR="$REPO_ROOT/core"
BIN_DIR="$REPO_ROOT/bin"
BUILD_DIR="$REPO_ROOT/build"

source "$BIN_DIR/lib/dev_mode.sh"

echo "🏗️  Building Vaulthalla core..."

vh_assert_dev_mode_consistency

DEV_MODE=false
vh_is_dev_mode && DEV_MODE=true

CLEAN_BUILD=false
BUILD_MANPAGE=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    -m|--manpage)
      BUILD_MANPAGE=true
      shift
      ;;
    --clean)
      CLEAN_BUILD=true
      shift
      ;;
    -h|--help)
      echo "Usage: $0 [--clean] [--manpage]"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
  esac
done

MESON_ARGS=()

if [[ "$DEV_MODE" == true ]]; then
  MESON_ARGS+=("-Dbuildtype=debug")
else
  MESON_ARGS+=("-Dbuildtype=release")
fi

if [[ "$BUILD_MANPAGE" == true ]]; then
  MESON_ARGS+=("-Dmanpage=true")
else
  MESON_ARGS+=("-Dmanpage=false")
fi

if [[ "$CLEAN_BUILD" == true && -d "$BUILD_DIR" ]]; then
  rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

meson setup "$BUILD_DIR" "$REPO_ROOT" "${MESON_ARGS[@]}" -Db_sanitize=address,undefined --reconfigure
meson compile -C "$BUILD_DIR"
sudo meson install -C "$BUILD_DIR"
sudo ldconfig
