#!/usr/bin/env bash
# Surface-aware verification for the vaulthalla monorepo.
# Shared by contributors and agents (.claude/skills/verify wraps it).
# Usage: tools/dev/verify.sh <profile> [...more profiles]
#   doctor      toolchain + repo sanity (no builds)
#   changed     infer profiles from `git diff` + untracked files vs HEAD (default)
#   core        meson compile + meson test in build/ (configures build/ if missing)
#   web         pnpm typecheck + lint (VERIFY_STRICT_LINT=0 downgrades lint failures to warnings)
#   release     tools.release check + release-tooling unittest suite
#   packaging   packaging contract tests (orphaned from `unittest discover`; run by module)
#   lifecycle   deploy/lifecycle unit tests
#   docs        payload-markdown checker on changed docs (or all docs) + pmdocs validate
#   shell       bash -n on changed/all bin/ and debian maintainer scripts
#   integration DESTRUCTIVE: make uninstall && make clean-full && make run_test (requires VERIFY_ALLOW_DESTRUCTIVE=1)
#   all         core web release packaging lifecycle docs shell
set -euo pipefail

ROOT_DIR="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
cd "$ROOT_DIR"

log()  { printf '[verify] %s\n' "$*"; }
warn() { printf '[verify] WARN: %s\n' "$*" >&2; }
die()  { printf '[verify] ERROR: %s\n' "$*" >&2; exit 1; }

changed_files() {
  { git diff --name-only --diff-filter=ACMRTUXB HEAD; git ls-files --others --exclude-standard; } | sort -u
}

run_doctor() {
  local c want have
  for c in git python3 meson ninja node pnpm; do
    if command -v "$c" >/dev/null; then log "$c: $("$c" --version 2>&1 | head -n1)"; else warn "$c missing"; fi
  done
  for c in pmdocs shellcheck dpkg-buildpackage; do command -v "$c" >/dev/null || warn "$c missing (optional)"; done
  want="$(tr -d '[:space:]' < web/.nvmrc)"; have="$(node -v 2>/dev/null | sed 's/^v//')"
  [[ "$have" == "$want"* ]] || warn "node ${have:-none} does not match web/.nvmrc $want"
  want="$(python3 -c 'import json;print(json.load(open("web/package.json"))["packageManager"].split("@")[1])')"
  have="$(pnpm -v 2>/dev/null || true)"
  [[ "$have" == "$want" ]] || warn "pnpm ${have:-none} does not match packageManager $want"
  [[ -d web/node_modules ]] || warn "web/node_modules missing: pnpm --dir web install --frozen-lockfile"
  [[ -f build/build.ninja ]] || warn "build/ not configured: meson setup build -Dbuild_unit_tests=true"
  [[ -d web/public/icons/fa ]] || warn "private web icons missing (web build will fail); see docs/contributors/development-setup.md"
  [[ -z "$(git status --porcelain)" ]] || warn "working tree is dirty"
  python3 -m tools.release check >/dev/null && log "release-managed versions in sync ($(cat VERSION))" || warn "tools.release check failed"
}

run_core() {
  if [[ ! -f build/build.ninja ]]; then
    log "configuring build/ (-Dbuild_unit_tests=true)"
    meson setup build -Dbuild_unit_tests=true
  fi
  log "meson compile -C build"
  meson compile -C build
  log "meson test -C build (DB-backed tests need 'make test' + sourced deploy/vaulthalla.env)"
  if [[ -f deploy/vaulthalla.env ]]; then
    set -a; # shellcheck disable=SC1091
    source deploy/vaulthalla.env; set +a
  else
    warn "deploy/vaulthalla.env missing; DB-backed unit tests may fail (run 'make test' first)"
  fi
  meson test -C build --print-errorlogs
}

run_web() {
  local want have
  want="$(tr -d '[:space:]' < web/.nvmrc)"; have="$(node -v 2>/dev/null | sed 's/^v//')"
  [[ "$have" == "$want"* ]] || warn "node $have does not match web/.nvmrc $want"
  log "web typecheck"; pnpm --dir web typecheck
  log "web lint"
  if ! pnpm --dir web lint; then
    [[ "${VERIFY_STRICT_LINT:-1}" == "1" ]] && die "web lint failed"
    warn "web lint failed (VERIFY_STRICT_LINT=0, continuing)"
  fi
}

run_release() {
  log "tools.release check"; python3 -m tools.release check
  log "release-tooling unittest suite"
  python3 -m unittest discover -s tools/release/tests -p 'test_*.py'
}

run_packaging() {
  local mods
  mods=$(find tools/release/tests/packaging -maxdepth 1 -name 'test_*.py' | LC_ALL=C sort | sed 's|/|.|g; s|\.py$||')
  log "packaging contract tests (by module)"
  # shellcheck disable=SC2086
  python3 -m unittest $mods
}

run_lifecycle() {
  log "deploy/lifecycle tests"; python3 -m unittest deploy.lifecycle.tests.test_main
}

run_docs() {
  local checker=.claude/skills/payload-markdown/scripts/check_payload_markdown_doc.py files=()
  mapfile -t files < <(changed_files | grep -E '^docs/.*\.md$' || true)
  if [[ "${#files[@]}" -eq 0 ]]; then mapfile -t files < <(find docs -name '*.md' | LC_ALL=C sort); fi
  log "payload-markdown checker on ${#files[@]} file(s)"
  python3 "$checker" "${files[@]}"
  if command -v pmdocs >/dev/null; then log "pmdocs validate --source docs"; pmdocs validate --source docs
  else warn "pmdocs not installed; skipped package validation"; fi
}

run_shell() {
  local files=() f
  mapfile -t files < <(changed_files | grep -E '^(bin/.*\.sh|web/bin/.*\.sh|tools/.*\.sh|debian/(postinst|prerm|postrm|preinst))$' || true)
  if [[ "${#files[@]}" -eq 0 ]]; then
    mapfile -t files < <( { find bin web/bin tools -name '*.sh'; ls debian/{postinst,prerm,postrm,preinst} 2>/dev/null; } | LC_ALL=C sort)
  fi
  log "bash -n on ${#files[@]} script(s)"
  for f in "${files[@]}"; do [[ -f "$f" ]] && bash -n "$f"; done
  if command -v shellcheck >/dev/null; then shellcheck -S warning "${files[@]}" || warn "shellcheck findings (non-blocking)"
  else warn "shellcheck not installed; syntax check only"; fi
}

run_integration() {
  [[ "${VERIFY_ALLOW_DESTRUCTIVE:-0}" == "1" ]] || die "integration tears down local dev/test state; re-run with VERIFY_ALLOW_DESTRUCTIVE=1 after confirming with the maintainer"
  warn "tearing down local dev + test installs, then running the harness on /tmp/vh_mount"
  make uninstall && make clean-full && make run_test
}

infer_profiles() {
  local files; files="$(changed_files)"
  [[ -z "$files" ]] && { log "no changes vs HEAD"; return; }
  grep -qE '^core/|^meson\.build$|^meson\.options$'            <<<"$files" && echo core
  grep -qE '^web/'                                              <<<"$files" && echo web
  grep -qE '^tools/release/|^VERSION$|^meson\.build$|^web/package\.json$|^debian/changelog$|^\.github/' <<<"$files" && echo release
  grep -qE '^debian/|^deploy/(systemd|psql|nginx)/|^tools/release/(packaging|tests/packaging)/|^\.github/' <<<"$files" && echo packaging
  grep -qE '^deploy/lifecycle/'                                 <<<"$files" && echo lifecycle
  grep -qE '^docs/|^debian/README'                              <<<"$files" && echo docs
  grep -qE '^bin/|^web/bin/|^tools/.*\.sh$|^debian/(postinst|prerm|postrm)$'   <<<"$files" && echo shell
  return 0
}

profiles=("$@"); [[ "${#profiles[@]}" -eq 0 ]] && profiles=(changed)
expanded=()
for p in "${profiles[@]}"; do
  case "$p" in
    changed) mapfile -t -O "${#expanded[@]}" expanded < <(infer_profiles) ;;
    all) expanded+=(core web release packaging lifecycle docs shell) ;;
    doctor|core|web|release|packaging|lifecycle|docs|shell|integration) expanded+=("$p") ;;
    *) die "unknown profile '$p'" ;;
  esac
done
[[ "${#expanded[@]}" -eq 0 ]] && { log "nothing to verify"; exit 0; }

log "profiles: ${expanded[*]}"
for p in "${expanded[@]}"; do log "== $p"; "run_$p"; done
log "verification passed: ${expanded[*]}"
