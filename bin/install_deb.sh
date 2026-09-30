#!/usr/bin/env bash
set -euo pipefail

# Local Debian build helper (dev only). Publishing from here is retired: the only publication
# path is the tag-triggered release workflow, which publishes idempotently (never overwrites a
# published version), verifies sha256 in the APT index, and records the release.
#   Cut a release:    python3 -m tools.release cut-release patch --push
#   Emergency manual: python3 -m tools.release publish-deb --output-dir <dir> --require-enabled
#                     (same idempotency/sha256 checks; needs RELEASE_PUBLISH_MODE=nexus + Nexus creds)

# =========================[ CONFIG ]=========================
: "${ARTIFACT_ROOT:="$(pwd)/dist"}"


# ======================[ UTIL / PATHS ]======================
log() { printf "\033[1;36m[%s]\033[0m %s\n" "$(date +'%H:%M:%S')" "$*"; }
die() { printf "\033[1;31mERROR:\033[0m %s\n" "$*" >&2; exit 1; }
require() { command -v "$1" >/dev/null 2>&1 || die "Missing required command: $1"; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# =======================[ ARG PARSE ]========================
usage() {
  cat <<'EOF'
Usage: bin/install_deb.sh [--sign]

  --sign     Build signed source/changes (omit -us -uc)
  --push     RETIRED. Publishing happens only through the release workflow:
             python3 -m tools.release cut-release {patch|minor|major} --push

Environment:
  ARTIFACT_ROOT (default: ./dist)
EOF
}

SIGN=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --push)
      die "--push is retired: this script no longer uploads to Nexus. Publish through the release workflow:
  python3 -m tools.release cut-release {patch|minor|major} --push
or, for an emergency manual publish with the same overwrite protection:
  python3 -m tools.release publish-deb --output-dir <dir> --require-enabled" ;;
    --sign) SIGN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "Unknown arg: $1" ;;
  esac
done

# =====================[ PRE-FLIGHT ]=========================
require git
require gbp
require dpkg-buildpackage
require dpkg-parsechangelog
require sed
require awk
require pandoc

# cleanup old build dir if it exists
rm -rf "$ARTIFACT_ROOT"

rm -rf build
meson setup build
MESON_VERSION="$(meson introspect --projectinfo build 2>/dev/null \
                        | sed -En 's/.*\"version\": *\"([^\"]+)\".*/\1/p')"

# Ensure we’re in a git repo and on correct branch
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "Not in a git repo"
CURRENT_BRANCH="$(git rev-parse --abbrev-ref HEAD)"
[[ -n "$CURRENT_BRANCH" ]] || die "Cannot determine current branch"

# Get package name from debian/control (Source:)
PKG_NAME="$(awk '/^Source:/ {print tolower($2); exit}' debian/control)"
[[ -n "$PKG_NAME" ]] || die "Failed to parse Source: from debian/control"

# Use DEBEMAIL/DEBFULLNAME or fall back to git config for dch
export DEBEMAIL="${DEBEMAIL:-$(git config --get user.email || true)}"
export DEBFULLNAME="${DEBFULLNAME:-$(git config --get user.name || true)}"
[[ -n "$DEBEMAIL" && -n "$DEBFULLNAME" ]] || die "Set DEBEMAIL/DEBFULLNAME or configure git user.name/user.email"

# ===================[ UPDATE CHANGELOG ]=====================
log "Running changelog script..."
if ! ./bin/generate_debian_changelog.sh; then
  die "generate_debian_changelog.sh failed"
fi

# =====================[ ISOLATED BUILD ]=====================
TMP_BUILD_ROOT="$(mktemp -d)"
cleanup() { rm -rf "$TMP_BUILD_ROOT"; }
trap cleanup EXIT

log "Creating isolated build tree…"
# Preserve .git so dpkg-parsechangelog etc still behave if needed
rsync -a --exclude "$ARTIFACT_ROOT" "$REPO_ROOT/" "$TMP_BUILD_ROOT/src/"

# Ensure /etc/vaulthalla/config.yaml exists
if [[ ! -f /etc/vaulthalla/config.yaml ]]; then
  log "/etc/vaulthalla/config.yaml not found, copying default config"
  sudo mkdir -p /etc/vaulthalla
  sudo cp -v "$REPO_ROOT/deploy/config/config.yaml" /etc/vaulthalla/
fi

cd "$TMP_BUILD_ROOT/src"

log "Building packages…"
if [[ $SIGN -eq 1 ]]; then
  dpkg-buildpackage -b
else
  dpkg-buildpackage -us -uc -b
fi

log "Collecting artifacts → $ARTIFACT_ROOT"
mkdir -p "$ARTIFACT_ROOT"
cd "$TMP_BUILD_ROOT"
shopt -s nullglob
for f in *.deb *.dsc *.changes *.buildinfo *.orig.tar.* *.debian.tar.*; do
  cp -v "$f" "$ARTIFACT_ROOT/"
done
shopt -u nullglob

log "Artifacts are in: $ARTIFACT_ROOT (local build only; publishing goes through the release workflow)"

log "✅ Done: $MESON_VERSION"
