#!/usr/bin/env bash
# Release web build (release.toml [debian] pre_build): private icons, a clean production build that
# debian/rules packages (web/.next/standalone + web/.next/static + web/public), and the standalone deployable
# tarball attached to the GitHub release: <output-dir>/vaulthalla-web_<VERSION>_next-standalone.tar.gz.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WEB_DIR="$REPO_ROOT/web"
OUTPUT_DIR="${1:?usage: build_release_payload.sh <output-dir>}"
[[ "$OUTPUT_DIR" = /* ]] || OUTPUT_DIR="$REPO_ROOT/$OUTPUT_DIR"
VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/VERSION")"

command -v pnpm >/dev/null 2>&1 || { echo "pnpm is not on PATH" >&2; exit 1; }

bash "$WEB_DIR/bin/sync_private_icons.sh"

echo "[web] pnpm install --frozen-lockfile"
pnpm --dir "$WEB_DIR" install --frozen-lockfile
echo "[web] pnpm build"
rm -rf "$WEB_DIR/.next"
pnpm --dir "$WEB_DIR" build

for required in "$WEB_DIR/.next/standalone/server.js" "$WEB_DIR/.next/static"; do
  [[ -e "$required" ]] || { echo "Next.js build output missing: $required" >&2; exit 1; }
done

# Stale tarballs from other versions would otherwise be picked up as release assets.
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"
staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT
mkdir -p "$staging/vaulthalla-web/.next"
# Keep symlinks (pnpm links inside the standalone output) as they are.
cp -a "$WEB_DIR/.next/standalone/." "$staging/vaulthalla-web/"
cp -a "$WEB_DIR/.next/static" "$staging/vaulthalla-web/.next/"
[[ -d "$WEB_DIR/public" ]] && cp -a "$WEB_DIR/public" "$staging/vaulthalla-web/"
archive="$OUTPUT_DIR/vaulthalla-web_${VERSION}_next-standalone.tar.gz"
tar -C "$staging" -czf "$archive" vaulthalla-web
echo "[web] wrote $archive"
