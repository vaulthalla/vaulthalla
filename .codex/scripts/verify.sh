#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

profile="${1:-web}"

run_web_checks() {
  [[ -n "${WEB_TYPECHECK_CMD:-}" ]] || die "WEB_TYPECHECK_CMD missing in .codex/config/project.env"
  [[ -n "${WEB_LINT_CMD:-}" ]] || die "WEB_LINT_CMD missing in .codex/config/project.env"
  [[ -n "${WEB_TEST_CMD:-}" ]] || die "WEB_TEST_CMD missing in .codex/config/project.env"

  log "Web typecheck: $WEB_TYPECHECK_CMD"
  run_eval_in "${WEB_DIR:-web}" "$WEB_TYPECHECK_CMD"

  log "Web lint: $WEB_LINT_CMD"
  if ! run_eval_in "${WEB_DIR:-web}" "$WEB_LINT_CMD"; then
    if [[ "${VERIFY_STRICT_LINT:-0}" == "1" ]]; then
      die "Web lint failed (VERIFY_STRICT_LINT=1)"
    fi
    warn "Web lint failed; continuing because VERIFY_STRICT_LINT is not enabled"
  fi

  log "Web test: $WEB_TEST_CMD"
  run_eval_in "${WEB_DIR:-web}" "$WEB_TEST_CMD"
}

run_release_checks() {
  [[ -n "${RELEASE_CHECK_CMD:-}" ]] || die "RELEASE_CHECK_CMD missing in .codex/config/project.env"
  log "Release check: $RELEASE_CHECK_CMD"
  eval "$RELEASE_CHECK_CMD"
}

case "$profile" in
  web)
    log "Running verification profile: web"
    run_web_checks
    ;;
  release)
    log "Running verification profile: release"
    run_release_checks
    ;;
  all)
    log "Running verification profile: all"
    run_web_checks
    run_release_checks
    ;;
  *)
    die "Unknown profile '$profile'. Use: web | release | all"
    ;;
esac

log "verification passed ($profile)"
