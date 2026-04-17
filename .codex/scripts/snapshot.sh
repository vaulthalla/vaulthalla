#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

mkdir -p "${CONTEXT_DIR:-.codex/context}"

timestamp="$(date '+%Y-%m-%d_%H-%M-%S')"
out_file="${CONTEXT_DIR:-.codex/context}/snapshot_${timestamp}.md"

{
  echo "# Work Snapshot"
  echo
  echo "- Timestamp: $(date -Iseconds)"
  echo "- Branch: $(git rev-parse --abbrev-ref HEAD)"
  echo "- Commit: $(git rev-parse --short HEAD)"
  echo
  echo "## Git Status"
  echo
  echo '```'
  git status --short
  echo '```'
  echo
  echo "## Recent Commits"
  echo
  echo '```'
  git log --oneline -n 10
  echo '```'
  echo
  echo "## Diff Summary"
  echo
  echo '```'
  git diff --stat
  echo '```'
} > "$out_file"

log "Snapshot written: $out_file"
