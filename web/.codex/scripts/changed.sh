#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

mode="${1:-lint}"

mapfile -t changed < <(git diff --name-only --diff-filter=ACMRTUXB -- '*.ts' '*.tsx' '*.js' '*.jsx')

if [[ "${#changed[@]}" -eq 0 ]]; then
  log "No changed TS/JS files detected"
  exit 0
fi

log "Changed files:"
printf ' - %s\n' "${changed[@]}"

case "$mode" in
  lint)
    log "Linting changed files"
    npx eslint "${changed[@]}"
    ;;
  typecheck)
    log "Typechecking project (TypeScript cannot safely isolate changed files only)"
    npx tsc --noEmit
    ;;
  all)
    log "Linting changed files"
    npx eslint "${changed[@]}"
    log "Typechecking project"
    npx tsc --noEmit
    ;;
  *)
    die "Unknown mode '$mode'. Use: lint | typecheck | all"
    ;;
esac

log "changed-file checks passed"
