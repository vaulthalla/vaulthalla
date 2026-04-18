#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$CORE_DIR/.." && pwd)"

source "$ROOT_DIR/bin/lib/dev_mode.sh"

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

if [[ "$CLEAN_BUILD" == true && -d "$CORE_DIR/build" ]]; then
  rm -rf "$CORE_DIR/build"
fi

mkdir -p "$CORE_DIR/build"

meson setup "$CORE_DIR/build" "$CORE_DIR" "${MESON_ARGS[@]}" -Db_sanitize=address,undefined --reconfigure
meson compile -C "$CORE_DIR/build"
sudo meson install -C "$CORE_DIR/build"
sudo ldconfig
