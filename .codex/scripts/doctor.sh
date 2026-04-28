#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

log "Running environment doctor for ${PROJECT_NAME:-project}"

have_cmd git || die "git is not installed"
have_cmd python3 || die "python3 is not installed"
have_cmd node || warn "node is not installed (web checks will fail)"
have_cmd pnpm || warn "pnpm is not installed (web checks will fail)"
have_cmd meson || warn "meson is not installed (core build/install scripts will fail)"
have_cmd ninja || warn "ninja is not installed (core build/install scripts will fail)"

log "git:  $(git --version)"
log "python3: $(python3 --version | head -n 1)"

if have_cmd node; then
  log "node: $(node -v)"
fi
if have_cmd pnpm; then
  log "pnpm: $(pnpm -v)"
fi
if have_cmd meson; then
  log "meson: $(meson --version)"
fi
if have_cmd ninja; then
  log "ninja: $(ninja --version)"
fi

if [[ -f "web/.nvmrc" ]]; then
  wanted="$(tr -d '[:space:]' < web/.nvmrc)"
  log "web/.nvmrc: $wanted"
fi

if [[ ! -f "meson.build" ]]; then
  warn "missing meson.build"
fi

if [[ ! -f "meson.options" ]]; then
  warn "missing meson.options"
fi

if [[ ! -f "core/meson.build" ]]; then
  warn "missing core/meson.build"
fi

if [[ ! -f "web/package.json" ]]; then
  warn "missing web/package.json"
fi

if [[ ! -f "VERSION" ]]; then
  warn "missing VERSION"
fi

if [[ ! -d "web/node_modules" ]]; then
  warn "web/node_modules missing; run '(cd web && pnpm install)'"
else
  log "web/node_modules: present"
fi

if [[ ! -d "build" ]]; then
  warn "build directory missing (expected before local core runtime tests)"
else
  log "build directory: present"
fi

if [[ -n "$(git status --porcelain)" ]]; then
  warn "working tree is dirty"
else
  log "working tree: clean"
fi

log "doctor completed"
