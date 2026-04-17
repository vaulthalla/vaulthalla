#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

log "Running environment doctor for ${PROJECT_NAME:-project}"

command -v node >/dev/null 2>&1 || die "node is not installed"
command -v pnpm >/dev/null 2>&1 || die "pnpm is not installed"
command -v git >/dev/null 2>&1 || die "git is not installed"

log "node: $(node -v)"
log "pnpm: $(pnpm -v)"
log "git:  $(git --version)"

if [[ -f ".nvmrc" ]]; then
  wanted="$(tr -d '[:space:]' < .nvmrc)"
  log ".nvmrc: $wanted"
fi

if [[ ! -d "node_modules" ]]; then
  warn "node_modules missing; run 'pnpm install'"
else
  log "node_modules: present"
fi

if [[ -n "$(git status --porcelain)" ]]; then
  warn "working tree is dirty"
else
  log "working tree: clean"
fi

log "doctor completed"
