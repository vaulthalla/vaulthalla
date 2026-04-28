#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  ./bin/clean.sh [--full] [--package] [--help]

Removes generated Vaulthalla build/package artifacts.

Default behavior:
  Removes non-primary generated artifacts while preserving ./build so Meson
  incremental builds are not destroyed by ordinary cleanup.

Options:
  --full      Also remove ./build and other primary build directories.
  --package   Remove Debian/package artifacts.
  --help      Show this help.

Examples:
  ./bin/clean.sh
  ./bin/clean.sh --package
  ./bin/clean.sh --full --package
EOF
}

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

FULL=false
PACKAGE=false

while (($#)); do
  case "$1" in
    --full|--purge)
      FULL=true
      ;;
    --package)
      PACKAGE=true
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

cd "$REPO_ROOT"

remove_paths() {
  local label="$1"
  shift

  echo "🧼 Removing $label..."
  for path in "$@"; do
    if compgen -G "$path" > /dev/null; then
      rm -rf $path
      echo "  removed $path"
    else
      echo "  skipped $path"
    fi
  done
}

# Conservative default cleanup:
# Keep ./build so Meson incremental builds survive normal cleanup.
DEFAULT_BUILD_ARTIFACTS=(
  "build-asan"
  "build-check"
  "build-check-install"
  "build-install"
  "core/build"
  "release"
)

FULL_BUILD_ARTIFACTS=(
  "build"
  "${DEFAULT_BUILD_ARTIFACTS[@]}"
  "obj-*"
)

PACKAGE_ARTIFACTS=(
  "obj-*"
  "debian/.debhelper"
  "debian/vaulthalla"
  "debian/files"
  "debian/*.substvars"
  "debian/*.debhelper.log"
)

if [[ "$FULL" == true ]]; then
  remove_paths "all build artifacts" "${FULL_BUILD_ARTIFACTS[@]}"
else
  remove_paths "secondary build artifacts" "${DEFAULT_BUILD_ARTIFACTS[@]}"
fi

if [[ "$PACKAGE" == true ]]; then
  remove_paths "Debian package artifacts" "${PACKAGE_ARTIFACTS[@]}"
fi

echo "✅ Clean complete."
