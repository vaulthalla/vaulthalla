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

run_core_ci_checks() {
  [[ -n "${CORE_CI_BUILD_CMD:-}" ]] || die "CORE_CI_BUILD_CMD missing in .codex/config/project.env"
  [[ -n "${CORE_CI_TEST_CMD:-}" ]] || die "CORE_CI_TEST_CMD missing in .codex/config/project.env"

  log "Core CI build: $CORE_CI_BUILD_CMD"
  eval "$CORE_CI_BUILD_CMD"

  log "Core CI test: $CORE_CI_TEST_CMD"
  eval "$CORE_CI_TEST_CMD"
}

run_clean_integration_checks() {
  [[ -n "${CORE_INTEGRATION_CLEAN_CMD:-}" ]] || die "CORE_INTEGRATION_CLEAN_CMD missing in .codex/config/project.env"
  warn "This profile intentionally tears down dev/prod and integration test state before rebuilding."
  warn "It validates the integration harness on /tmp/vh_mount, not the production /mnt/vaulthalla mount."
  log "Clean integration test: $CORE_INTEGRATION_CLEAN_CMD"
  eval "$CORE_INTEGRATION_CLEAN_CMD"
}

case "$profile" in
  web)
    log "Running verification profile: web"
    run_web_checks
    ;;
  core)
    log "Running verification profile: core"
    run_core_ci_checks
    ;;
  integration)
    log "Running verification profile: integration"
    run_clean_integration_checks
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
    die "Unknown profile '$profile'. Use: web | core | integration | release | all"
    ;;
esac

log "verification passed ($profile)"
