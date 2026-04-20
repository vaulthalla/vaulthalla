#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

mode="${1:-all}"

mapfile -t web_changed < <(git diff --name-only --diff-filter=ACMRTUXB -- 'web/**/*.ts' 'web/**/*.tsx' 'web/**/*.js' 'web/**/*.jsx')
mapfile -t release_changed < <(git diff --name-only --diff-filter=ACMRTUXB -- 'tools/release/**/*.py' 'VERSION' 'core/meson.build' 'web/package.json' 'debian/changelog')

run_web_changed_checks() {
  if [[ "${#web_changed[@]}" -eq 0 ]]; then
    log "No changed web TS/JS files detected"
    return
  fi

  log "Changed web files:"
  printf ' - %s\n' "${web_changed[@]}"

  mapfile -t rel_web_changed < <(printf '%s\n' "${web_changed[@]}" | sed 's|^web/||')

  log "Linting changed web files"
  run_in "${WEB_DIR:-web}" npx eslint "${rel_web_changed[@]}"

  log "Typechecking web project"
  run_in "${WEB_DIR:-web}" npx tsc --noEmit
}

run_release_changed_checks() {
  if [[ "${#release_changed[@]}" -eq 0 ]]; then
    log "No changed release-managed files detected"
    return
  fi

  log "Changed release-managed files:"
  printf ' - %s\n' "${release_changed[@]}"

  if [[ -n "${RELEASE_CHECK_CMD:-}" ]]; then
    log "Running release check"
    eval "$RELEASE_CHECK_CMD"
  else
    warn "RELEASE_CHECK_CMD is not configured; skipping release drift check"
  fi
}

case "$mode" in
  web)
    run_web_changed_checks
    ;;
  release)
    run_release_changed_checks
    ;;
  all)
    run_web_changed_checks
    run_release_changed_checks
    ;;
  *)
    die "Unknown mode '$mode'. Use: web | release | all"
    ;;
esac

log "changed-file checks passed ($mode)"
