#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

mkdir -p "${CONTEXT_DIR:-.codex/context}"
out_file="${CONTEXT_DIR:-.codex/context}/generated-index.md"

log "Refreshing monorepo index: $out_file"

{
  echo "# Generated Monorepo Index"
  echo
  echo "- Generated: $(date -Iseconds)"
  echo "- Branch: $(git rev-parse --abbrev-ref HEAD)"
  echo "- Commit: $(git rev-parse --short HEAD)"
  echo
  echo "## Top-Level Layout"
  echo
  echo '```'
  find . -mindepth 1 -maxdepth 1 -type d | sort
  echo '```'
  echo
  echo "## Core Protocol Surfaces"
  echo
  echo '```'
  find core/include/protocols core/src/protocols -maxdepth 3 -type f | sort
  echo '```'
  echo
  echo "## Web App Surfaces"
  echo
  echo '```'
  find web/src -maxdepth 3 -type d | sort
  echo '```'
  echo
  echo "## Release + Packaging Surfaces"
  echo
  echo '```'
  find tools/release debian deploy -maxdepth 3 -type f | sort
  echo '```'
} > "$out_file"

log "Index refreshed"
