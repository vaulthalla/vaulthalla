#!/usr/bin/env bash
set -euo pipefail

# =========================[ CONFIG ]=========================
: "${NEXUS_VAULTHALLA_REPO:=https://apt.vaulthalla.sh}"
: "${NEXUS_VALKYRIAN_REPO:=https://apt.valkyrianlabs.com}"
: "${UPLOAD_USER:=cooper}"
: "${UPLOAD_PASS:=${NEXUS_COOPER_PASSWORD:-}}"
: "${ARTIFACT_ROOT:="$(pwd)/dist"}"

# Toggle: push artifacts when --push is used
PUSH=0


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
Usage: bin/release-deb.sh [--push] [--sign]

  --push     Upload resulting .deb/.dsc/.changes to Nexus APT repos
  --sign     Build signed source/changes (omit -us -uc)

Environment:
  NEXUS_VAULTHALLA_HOST, NEXUS_VALKYRIAN_HOST
  NEXUS_VAULTHALLA_REPO, NEXUS_VALKYRIAN_REPO
  UPLOAD_USER, UPLOAD_PASS (or NEXUS_COOPER_PASSWORD)
  DEBIAN_BRANCH (default: debian)
EOF
}

SIGN=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --push) PUSH=1; shift ;;
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
[[ $PUSH -eq 1 ]] && require curl

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

# ======================[ OPTIONAL PUSH ]=====================
if [[ $PUSH -eq 1 ]]; then
  [[ -n "$UPLOAD_PASS" ]] || die "UPLOAD_PASS is empty (set UPLOAD_PASS or NEXUS_COOPER_PASSWORD)"

  # Compute pool path: pool/main/<first>/<pkg>/
  first="$(printf "%s" "$PKG_NAME" | cut -c1)"

  log "Uploading to Nexus APT (Vaulthalla): $NEXUS_VAULTHALLA_REPO"
  for f in "$ARTIFACT_ROOT"/*.deb; do
    fname="$(basename "$f")"
    log "  → $fname"
    curl -sSf -u "$UPLOAD_USER:$UPLOAD_PASS" -H "Content-Type: multipart/form-data" \
                                                 --data-binary "@$f" \
                                                 "$NEXUS_VAULTHALLA_REPO"
  done

  log "Uploads complete. Nexus APT will handle indexing/signing per repo config."
else
  log "Skipping upload (no --push). Artifacts are in: $ARTIFACT_ROOT"
fi

log "✅ Done: $MESON_VERSION"
