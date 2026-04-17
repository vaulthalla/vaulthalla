#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

log "Running full verification"

if [[ -z "${TYPECHECK_CMD:-}" || -z "${LINT_CMD:-}" ]]; then
  die "TYPECHECK_CMD/LINT_CMD missing in .codex/config/project.env"
fi

log "Typecheck: $TYPECHECK_CMD"
eval "$TYPECHECK_CMD"

log "Lint: $LINT_CMD"
if ! eval "$LINT_CMD"; then
  if [[ "${VERIFY_STRICT_LINT:-0}" == "1" ]]; then
    die "Lint failed (VERIFY_STRICT_LINT=1)"
  fi
  warn "Lint failed; continuing because VERIFY_STRICT_LINT is not enabled"
fi

log "verification passed"
