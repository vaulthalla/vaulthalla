#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

if [[ -f ".codex/config/project.env" ]]; then
  # shellcheck disable=SC1091
  source ".codex/config/project.env"
fi

log() {
  printf '[codex] %s\n' "$*"
}

warn() {
  printf '[codex] WARN: %s\n' "$*" >&2
}

die() {
  printf '[codex] ERROR: %s\n' "$*" >&2
  exit 1
}

have_cmd() {
  command -v "$1" >/dev/null 2>&1
}

run_in() {
  local dir="$1"
  shift
  (
    cd "$dir"
    "$@"
  )
}

run_eval_in() {
  local dir="$1"
  local cmd="$2"
  (
    cd "$dir"
    eval "$cmd"
  )
}
