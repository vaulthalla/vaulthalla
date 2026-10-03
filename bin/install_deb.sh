#!/usr/bin/env bash
# Local release rehearsal (`make deb`): builds the release artifacts exactly as release CI does, from a throwaway
# worktree of HEAD, and copies them to ARTIFACT_ROOT. Nothing is committed, tagged or published, and this checkout's
# files (staged .release/ docs, debian/changelog) are never touched.
#   vlr prepare -> vlr build-deb (pre_build: icons + web build) -> vlr checksums -> vlr validate-artifacts
# Commit first: uncommitted changes are not part of HEAD. Publishing only happens in the tag-triggered release
# workflow (`vlr cut patch|minor|major --push`).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
: "${ARTIFACT_ROOT:="$REPO_ROOT/dist"}"

log() { printf "\033[1;36m[%s]\033[0m %s\n" "$(date +'%H:%M:%S')" "$*"; }
die() { printf "\033[1;31mERROR:\033[0m %s\n" "$*" >&2; exit 1; }

case "${1:-}" in
  "") ;;
  -h|--help) sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  --push) die "--push is retired: publish with 'vlr cut patch|minor|major --push' (release CI publishes)." ;;
  *) die "Unknown arg: $1" ;;
esac

for tool in vlr git dpkg-buildpackage pnpm; do
  command -v "$tool" >/dev/null 2>&1 || die "Missing required command: $tool"
done
[[ -z "$(git -C "$REPO_ROOT" status --porcelain --untracked-files=no)" ]] \
  || log "WARNING: uncommitted changes are not included; this builds HEAD ($(git -C "$REPO_ROOT" rev-parse --short HEAD))"

WORKTREE="$(mktemp -d "${TMPDIR:-/tmp}/vh-release-rehearsal.XXXXXX")"
cleanup() {
  git -C "$REPO_ROOT" worktree remove --force "$WORKTREE" >/dev/null 2>&1 || rm -rf "$WORKTREE"
  git -C "$REPO_ROOT" worktree prune >/dev/null 2>&1 || true
}
trap cleanup EXIT

log "Worktree of HEAD: $WORKTREE"
git -C "$REPO_ROOT" worktree add --detach "$WORKTREE" HEAD >/dev/null

cd "$WORKTREE"
vlr check --release
log "vlr prepare (work tree only)"
vlr prepare --record release/meta/prepare.json
log "vlr build-deb"
vlr build-deb
vlr checksums
vlr validate-artifacts

rm -rf "$ARTIFACT_ROOT"
mkdir -p "$ARTIFACT_ROOT"
cp -a release/. "$ARTIFACT_ROOT/"
log "Artifacts (local rehearsal only; nothing published):"
(cd "$ARTIFACT_ROOT" && vlr --repo "$WORKTREE" artifacts 2>/dev/null || ls -l)
log "Copied to $ARTIFACT_ROOT"
