#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

# shellcheck source=tools/e2e/load_env.sh
source "$REPO_ROOT/tools/e2e/load_env.sh"

RESULT_DIR="${S3_GATEWAY_E2E_RESULT_DIR:-$REPO_ROOT/test-results/s3-gateway-e2e}"
REPORT="$RESULT_DIR/merge-ready-report.txt"
ENV_REPORT="$RESULT_DIR/merge-ready-env-report.txt"
PREFIX_BASE="${S3_GATEWAY_MERGE_READY_PREFIX:-s3-gateway-merge-ready/$(date -u +%Y%m%dT%H%M%SZ)-$$}"
LOCAL_PREFIX="$PREFIX_BASE/local"
REMOTE_PREFIX="$PREFIX_BASE/remote"
FAILED=0
STAGE_ROWS=()
STAGE_LOGS=()
E2E_CREDENTIAL_STATUS="playwright-global-setup-per-run"
REMOTE_CLEANUP_RESULT="not-run"

install -d -m 0755 "$RESULT_DIR"

record_stage() {
  local name="$1"
  local status="$2"
  local command="$3"
  local log="$4"
  STAGE_ROWS+=("$status|$name|$command|$log")
  STAGE_LOGS+=("$log")
  if [[ "$status" != "PASS" && "$status" != "SKIP" ]]; then
    FAILED=1
  fi
}

run_stage() {
  local name="$1"
  local command="$2"
  local log="$RESULT_DIR/merge-ready-${name//[^A-Za-z0-9_.-]/-}.log"
  echo
  echo "== $name =="
  echo "$command"
  if bash -lc "$command" >"$log" 2>&1; then
    echo "PASS $name"
    record_stage "$name" "PASS" "$command" "$log"
  else
    echo "FAIL $name"
    tail -n 80 "$log" || true
    record_stage "$name" "FAIL" "$command" "$log"
  fi
}

write_report() {
  {
    echo "S3 Gateway merge readiness report"
    echo "Generated: $(date -Iseconds)"
    echo "Repository: $REPO_ROOT"
    echo "Branch: $(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
    echo "Commit: $(git rev-parse HEAD 2>/dev/null || echo unknown)"
    echo
    echo "Environment"
    echo "Env report: $ENV_REPORT"
    echo "E2E credentials: $E2E_CREDENTIAL_STATUS"
    echo "Local smoke prefix: $LOCAL_PREFIX"
    echo "Remote smoke prefix: $REMOTE_PREFIX"
    echo "R2 configured: $(vh_e2e_has_r2_env && echo yes || echo no)"
    echo "R2 cleanup result: $REMOTE_CLEANUP_RESULT"
    echo
    echo "Stages"
    local row status name command log
    for row in "${STAGE_ROWS[@]}"; do
      IFS='|' read -r status name command log <<<"$row"
      echo "- $status: $name"
      echo "  command: $command"
      echo "  log: $log"
    done
  } >"$REPORT"
}

vh_e2e_redacted_env_report | tee "$ENV_REPORT"

run_stage "shell-syntax" \
  "bash -n tools/e2e/load_env.sh tools/e2e/provision_e2e_user.sh tools/smoke/s3_gateway_e2e.sh tools/smoke/s3_gateway_scoped_budget_smoke.sh tools/smoke/s3_gateway_merge_ready.sh"
run_stage "meson-compile" "meson compile -C build"
run_stage "db-backed-s3-gateway-cost-pricing-tests" \
  "meson test -C build vh_unit_tests --print-errorlogs --test-args='--gtest_filter=S3GatewayDbTest.*:S3CostSafetyTest.*Gateway*:S3PricingTest.*Gateway*'"
run_stage "web-unit-tests" "pnpm --dir web run test"
run_stage "playwright-s3-gateway" "pnpm --dir web run test:e2e:s3-gateway"
run_stage "local-smoke" "tools/smoke/s3_gateway_e2e.sh --use-build-runtime --local-only --prefix '$LOCAL_PREFIX'"

if vh_e2e_has_r2_env; then
  run_stage "remote-smoke" "tools/smoke/s3_gateway_e2e.sh --use-build-runtime --require-remote --prefix '$REMOTE_PREFIX'"
  remote_log="$RESULT_DIR/merge-ready-remote-smoke.log"
  if [[ -f "$remote_log" ]]; then
    REMOTE_CLEANUP_RESULT="$(grep -E 'remote cleanup:' "$remote_log" | tail -1 | sed -E 's/.*remote cleanup:[[:space:]]*//')"
    [[ -n "$REMOTE_CLEANUP_RESULT" ]] || REMOTE_CLEANUP_RESULT="unknown"
  else
    REMOTE_CLEANUP_RESULT="unknown"
  fi
else
  echo
  echo "== remote-smoke =="
  echo "SKIP remote-smoke (R2 env not configured)"
  record_stage "remote-smoke" "SKIP" "tools/smoke/s3_gateway_e2e.sh --use-build-runtime --require-remote --prefix '$REMOTE_PREFIX'" ""
  REMOTE_CLEANUP_RESULT="skipped-no-r2-env"
fi

run_stage "git-diff-check" "git diff --check"

write_report
echo
if [[ "$FAILED" == "0" ]]; then
  echo "S3 Gateway merge readiness: PASS"
else
  echo "S3 Gateway merge readiness: FAIL"
fi
echo "Report: $REPORT"

exit "$FAILED"
