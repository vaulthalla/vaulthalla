#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"
# shellcheck source=tools/e2e/load_env.sh
source "$REPO_ROOT/tools/e2e/load_env.sh"

VH_BIN="${VH_BIN:-vh}"
AWS_BIN="${AWS_BIN:-aws}"
MC_BIN="${MC_BIN:-mc}"
JQ_BIN="${JQ_BIN:-jq}"
ENDPOINT="${S3_GATEWAY_ENDPOINT:-http://127.0.0.1:39000}"
SMOKE_RUN_ID="${S3_GATEWAY_SMOKE_RUN_ID:-$$}"
LOCAL_BUCKET="${S3_GATEWAY_SMOKE_LOCAL_BUCKET:-vh-smoke-local-$SMOKE_RUN_ID}"
OTHER_BUCKET="${S3_GATEWAY_SMOKE_OTHER_BUCKET:-vh-smoke-denied-$SMOKE_RUN_ID}"
REMOTE_BUCKET="${S3_GATEWAY_SMOKE_REMOTE_BUCKET:-vh-smoke-remote-$SMOKE_RUN_ID}"
CRED_NAME="${S3_GATEWAY_SMOKE_CRED_NAME:-vh-smoke-scoped-budget}"
MONTHLY_LIMIT="${S3_GATEWAY_SMOKE_MONTHLY_LIMIT:-0.00000001}"
MC_ALIAS="${S3_GATEWAY_SMOKE_MC_ALIAS:-vh-smoke}"
UPSTREAM_API_KEY="${S3_GATEWAY_SMOKE_API_KEY:-}"
UPSTREAM_API_KEY_NAME="${S3_GATEWAY_SMOKE_UPSTREAM_API_KEY_NAME:-vh-smoke-upstream-$$}"
UPSTREAM_BUCKET="${S3_GATEWAY_SMOKE_UPSTREAM_BUCKET:-${VAULTHALLA_TEST_R2_BUCKET:-}}"
UPSTREAM_ACCESS_KEY="${S3_GATEWAY_SMOKE_UPSTREAM_ACCESS_KEY:-${VAULTHALLA_TEST_R2_ACCESS_KEY:-}}"
UPSTREAM_SECRET_KEY="${S3_GATEWAY_SMOKE_UPSTREAM_SECRET_KEY:-${VAULTHALLA_TEST_R2_SECRET_ACCESS_KEY:-}}"
UPSTREAM_ENDPOINT="${S3_GATEWAY_SMOKE_UPSTREAM_ENDPOINT:-${VAULTHALLA_TEST_R2_ENDPOINT:-}}"
UPSTREAM_REGION="${S3_GATEWAY_SMOKE_UPSTREAM_REGION:-${VAULTHALLA_TEST_R2_REGION:-auto}}"
UPSTREAM_PROVIDER="${S3_GATEWAY_SMOKE_UPSTREAM_PROVIDER:-cloudflare-r2}"
PREFIX="${S3_GATEWAY_SMOKE_PREFIX:-s3-gateway-test/$(date -u +%Y%m%dT%H%M%SZ)-$$}"
LOCAL_ONLY=0
REQUIRE_REMOTE=0
KEEP_RESOURCES=0
CREATED_UPSTREAM_API_KEY=0
REMOTE_CREATED=0
TARGET_BUCKET=""
MODE_USED="local"
ACCESS_KEY=""
SECRET_KEY=""
CREDENTIAL_USED=""
REMOTE_CLEANUP_STATUS="not-run"
GATEWAY_CLEANUP_STATUS="not-run"
CLEANUP_TIMEOUT="${S3_GATEWAY_SMOKE_CLEANUP_TIMEOUT:-60}"

usage() {
  cat <<'USAGE'
Usage: tools/smoke/s3_gateway_scoped_budget_smoke.sh [options]

Options:
  --local-only       Exercise only local gateway bucket behavior.
  --require-remote   Fail if an R2/S3 remote-cache bucket cannot be created.
  --keep-resources   Do not revoke credentials or delete the test prefix.
  --prefix <prefix>  Prefix for all test objects. Defaults to s3-gateway-test/<timestamp>-<pid>.
  -h, --help         Show this help.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --local-only)
      LOCAL_ONLY=1
      shift
      ;;
    --require-remote)
      REQUIRE_REMOTE=1
      shift
      ;;
    --keep-resources)
      KEEP_RESOURCES=1
      shift
      ;;
    --prefix)
      if [[ $# -lt 2 || -z "$2" ]]; then
        echo "--prefix requires a value" >&2
        exit 2
      fi
      PREFIX="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

PREFIX="${PREFIX#/}"
PREFIX="${PREFIX%/}"
if [[ -z "$PREFIX" ]]; then
  echo "prefix must not be empty" >&2
  exit 2
fi

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing required command: $1" >&2
    exit 2
  fi
}

has_cmd() {
  command -v "$1" >/dev/null 2>&1
}

json_value() {
  "$JQ_BIN" -r "$1"
}

run_vh() {
  "$VH_BIN" "$@"
}

redact_secret_output() {
  sed -E \
    -e 's/(Access Key: ).*/\1<redacted>/I' \
    -e 's/(Secret Access Key: ).*/\1<redacted>/I' \
    -e 's/(Secret Key: ).*/\1<redacted>/I' \
    -e 's/(--access[= ]+)[^ ]+/\1<redacted>/gI' \
    -e 's/(--secret[= ]+)[^ ]+/\1<redacted>/gI'
}

run_with_timeout() {
  local seconds="$1"
  shift
  if command -v timeout >/dev/null 2>&1; then
    timeout "$seconds" "$@"
  else
    "$@"
  fi
}

create_local_bucket() {
  local bucket="$1"
  if run_vh s3-gateway bucket create-local "$bucket"; then
    return 0
  fi
  echo "create-local failed for $bucket; continuing in case it already exists" >&2
}

gateway_rm_prefix() {
  [[ -n "$TARGET_BUCKET" && -n "$ACCESS_KEY" && "$KEEP_RESOURCES" != "1" ]] || return 0
  if [[ "$AWS_AVAILABLE" == "1" ]]; then
    AWS_ACCESS_KEY_ID="$ACCESS_KEY" \
    AWS_SECRET_ACCESS_KEY="$SECRET_KEY" \
    AWS_EC2_METADATA_DISABLED=true \
    AWS_DEFAULT_REGION="${AWS_DEFAULT_REGION:-us-east-1}" \
      run_with_timeout "$CLEANUP_TIMEOUT" "$AWS_BIN" --endpoint-url "$ENDPOINT" s3 rm "s3://$TARGET_BUCKET/$PREFIX/" --recursive >/dev/null
    return 0
  fi
  if [[ "$MC_AVAILABLE" == "1" ]]; then
    run_with_timeout "$CLEANUP_TIMEOUT" "$MC_BIN" rm --recursive --force "$MC_ALIAS/$TARGET_BUCKET/$PREFIX/" >/dev/null
    return 0
  fi
}

remote_rm_prefix() {
  [[ "$REMOTE_CREATED" == "1" && "$KEEP_RESOURCES" != "1" ]] || return 0
  [[ -n "$UPSTREAM_BUCKET" && -n "$UPSTREAM_ENDPOINT" ]] || return 0
  if [[ -n "$UPSTREAM_ACCESS_KEY" && -n "$UPSTREAM_SECRET_KEY" && "$AWS_AVAILABLE" == "1" ]]; then
    AWS_ACCESS_KEY_ID="$UPSTREAM_ACCESS_KEY" \
    AWS_SECRET_ACCESS_KEY="$UPSTREAM_SECRET_KEY" \
    AWS_EC2_METADATA_DISABLED=true \
    AWS_DEFAULT_REGION="$UPSTREAM_REGION" \
      run_with_timeout "$CLEANUP_TIMEOUT" "$AWS_BIN" --endpoint-url "$UPSTREAM_ENDPOINT" s3 rm "s3://$UPSTREAM_BUCKET/$PREFIX/" --recursive >/dev/null
    return 0
  fi
  if [[ -n "$UPSTREAM_ACCESS_KEY" && -n "$UPSTREAM_SECRET_KEY" && "$MC_AVAILABLE" == "1" ]]; then
    "$MC_BIN" alias set "${MC_ALIAS}-upstream" "$UPSTREAM_ENDPOINT" "$UPSTREAM_ACCESS_KEY" "$UPSTREAM_SECRET_KEY" >/dev/null
    run_with_timeout "$CLEANUP_TIMEOUT" "$MC_BIN" rm --recursive --force "${MC_ALIAS}-upstream/$UPSTREAM_BUCKET/$PREFIX/" >/dev/null
    "$MC_BIN" alias rm "${MC_ALIAS}-upstream" >/dev/null 2>&1 || true
    return 0
  fi
  return 0
}

cleanup_resources() {
  rm -f "${tmp_file:-}" "${unlisted_out:-}" "${budget_out:-}"

  if [[ "$KEEP_RESOURCES" == "1" ]]; then
    GATEWAY_CLEANUP_STATUS="kept"
    REMOTE_CLEANUP_STATUS="kept"
    return 0
  fi

  if gateway_rm_prefix; then
    GATEWAY_CLEANUP_STATUS="ok"
  else
    GATEWAY_CLEANUP_STATUS="failed"
    echo "GATEWAY CLEANUP FAILED for bucket=$TARGET_BUCKET prefix=$PREFIX" >&2
  fi

  if remote_rm_prefix; then
    REMOTE_CLEANUP_STATUS=$([[ "$REMOTE_CREATED" == "1" ]] && echo "ok" || echo "not-needed")
  else
    REMOTE_CLEANUP_STATUS="failed"
    echo "REMOTE CLEANUP FAILED for upstream_bucket=$UPSTREAM_BUCKET prefix=$PREFIX" >&2
  fi

  if [[ -n "${ACCESS_KEY:-}" ]]; then
    run_vh s3-gateway creds revoke "$ACCESS_KEY" >/dev/null 2>&1 || true
  else
    run_vh s3-gateway creds revoke "$CRED_NAME" >/dev/null 2>&1 || true
  fi
  if [[ "$CREATED_UPSTREAM_API_KEY" == "1" && -n "${UPSTREAM_API_KEY:-}" ]]; then
    run_vh api-key delete "$UPSTREAM_API_KEY" >/dev/null 2>&1 || true
  fi
  if [[ "${MC_AVAILABLE:-0}" == "1" ]]; then
    "$MC_BIN" alias rm "$MC_ALIAS" >/dev/null 2>&1 || true
  fi
}

finish() {
  local code=$?
  local result="FAIL"
  if [[ "$code" == "0" ]]; then result="PASS"; fi
  cleanup_resources || true
  echo
  echo "S3 gateway smoke summary: $result"
  echo "  endpoint: $ENDPOINT"
  echo "  bucket: ${TARGET_BUCKET:-not-created}"
  echo "  credential: ${CREDENTIAL_USED:-not-created}"
  echo "  mode: $MODE_USED"
  echo "  prefix: $PREFIX"
  echo "  upstream_bucket: ${UPSTREAM_BUCKET:-not-configured}"
  echo "  gateway cleanup: $GATEWAY_CLEANUP_STATUS"
  echo "  remote cleanup: $REMOTE_CLEANUP_STATUS"
  exit "$code"
}
trap finish EXIT

require_cmd "$VH_BIN"
require_cmd "$JQ_BIN"
AWS_AVAILABLE=0
MC_AVAILABLE=0
if has_cmd "$AWS_BIN"; then AWS_AVAILABLE=1; fi
if has_cmd "$MC_BIN"; then MC_AVAILABLE=1; fi
if [[ "${S3_GATEWAY_SMOKE_REQUIRE_AWS:-0}" == "1" ]]; then require_cmd "$AWS_BIN"; AWS_AVAILABLE=1; fi
if [[ "${S3_GATEWAY_SMOKE_REQUIRE_MC:-0}" == "1" ]]; then require_cmd "$MC_BIN"; MC_AVAILABLE=1; fi
if [[ "$AWS_AVAILABLE" != "1" && "$MC_AVAILABLE" != "1" ]]; then
  echo "missing S3 client: install aws or mc, or set AWS_BIN/MC_BIN" >&2
  exit 2
fi
USE_AWS="$AWS_AVAILABLE"
USE_MC=0
if [[ "$MC_AVAILABLE" == "1" && ( "$AWS_AVAILABLE" != "1" || "${S3_GATEWAY_SMOKE_REQUIRE_MC:-0}" == "1" ) ]]; then
  USE_MC=1
fi

echo "Smoke configuration"
echo "  endpoint: $ENDPOINT"
echo "  prefix: $PREFIX"
echo "  local bucket: $LOCAL_BUCKET"
echo "  remote bucket: $REMOTE_BUCKET"
echo "  upstream bucket: ${UPSTREAM_BUCKET:-not-configured}"
echo "  mode request: $([[ "$LOCAL_ONLY" == "1" ]] && echo local-only || ([[ "$REQUIRE_REMOTE" == "1" ]] && echo require-remote || echo auto))"

echo "1. Enable S3 gateway"
run_vh s3-gateway enable

echo "2. Create local buckets"
create_local_bucket "$LOCAL_BUCKET"
create_local_bucket "$OTHER_BUCKET"

TARGET_BUCKET="$LOCAL_BUCKET"
if [[ "$LOCAL_ONLY" != "1" ]]; then
  if [[ -z "$UPSTREAM_API_KEY" && -n "$UPSTREAM_ACCESS_KEY" && -n "$UPSTREAM_SECRET_KEY" && -n "$UPSTREAM_ENDPOINT" && -n "$UPSTREAM_BUCKET" ]]; then
    echo "3a. Create upstream API key from smoke environment"
    api_key_output=""
    if api_key_output="$(run_vh api-key create "$UPSTREAM_API_KEY_NAME" \
      --access "$UPSTREAM_ACCESS_KEY" \
      --secret "$UPSTREAM_SECRET_KEY" \
      --provider "$UPSTREAM_PROVIDER" \
      --endpoint "$UPSTREAM_ENDPOINT" \
      --region "$UPSTREAM_REGION" 2>&1)"; then
      printf '%s\n' "$api_key_output" | redact_secret_output
      UPSTREAM_API_KEY="$UPSTREAM_API_KEY_NAME"
      CREATED_UPSTREAM_API_KEY=1
    else
      printf '%s\n' "$api_key_output" | redact_secret_output >&2
      echo "upstream API key create failed; continuing in case $UPSTREAM_API_KEY_NAME already exists" >&2
      UPSTREAM_API_KEY="$UPSTREAM_API_KEY_NAME"
    fi
  fi

  if [[ -n "$UPSTREAM_API_KEY" && -n "$UPSTREAM_BUCKET" ]]; then
    echo "3. Create remote-cache bucket"
    if run_vh s3-gateway bucket create-remote-cache "$REMOTE_BUCKET" \
      --api-key "$UPSTREAM_API_KEY" \
      --upstream-bucket "$UPSTREAM_BUCKET" \
      --encrypt; then
      TARGET_BUCKET="$REMOTE_BUCKET"
      REMOTE_CREATED=1
      MODE_USED="remote-cache"
    elif [[ "$REQUIRE_REMOTE" == "1" ]]; then
      echo "remote-cache create failed and --require-remote was set" >&2
      exit 1
    else
      echo "remote-cache create failed; continuing with local-only scope checks" >&2
    fi
  elif [[ "$REQUIRE_REMOTE" == "1" ]]; then
    echo "--require-remote set but upstream API key/bucket configuration is unavailable" >&2
    exit 2
  else
    echo "3. Skipping remote-cache bucket; set S3_GATEWAY_SMOKE_API_KEY and S3_GATEWAY_SMOKE_UPSTREAM_BUCKET, or VAULTHALLA_TEST_R2_* environment variables"
  fi
else
  echo "3. Skipping remote-cache bucket due to --local-only"
fi

echo "4. Create scoped credential for $TARGET_BUCKET"
credential_json="$(run_vh s3-gateway creds create "$CRED_NAME" \
  --scope vault-allowlist \
  --vault "$TARGET_BUCKET" \
  --list --read --write --delete \
  --json)"
ACCESS_KEY="$(printf '%s\n' "$credential_json" | json_value '.access_key')"
SECRET_KEY="$(printf '%s\n' "$credential_json" | json_value '.secret_access_key')"
CREDENTIAL_USED="$ACCESS_KEY"

export AWS_ACCESS_KEY_ID="$ACCESS_KEY"
export AWS_SECRET_ACCESS_KEY="$SECRET_KEY"
export AWS_EC2_METADATA_DISABLED=true
export AWS_DEFAULT_REGION="${AWS_DEFAULT_REGION:-us-east-1}"

tmp_file="$(mktemp)"
unlisted_out="$(mktemp)"
budget_out="$(mktemp)"
printf 'vaulthalla scoped gateway smoke %s\n' "$(date -Iseconds)" >"$tmp_file"

echo "5. Prove allowed bucket works"
if [[ "$USE_AWS" == "1" ]]; then
  "$AWS_BIN" --endpoint-url "$ENDPOINT" s3 cp "$tmp_file" "s3://$TARGET_BUCKET/$PREFIX/allowed-aws.txt"
  "$AWS_BIN" --endpoint-url "$ENDPOINT" s3 ls "s3://$TARGET_BUCKET/$PREFIX/"
fi
if [[ "$USE_MC" == "1" ]]; then
  "$MC_BIN" alias set "$MC_ALIAS" "$ENDPOINT" "$ACCESS_KEY" "$SECRET_KEY" >/dev/null
  "$MC_BIN" cp "$tmp_file" "$MC_ALIAS/$TARGET_BUCKET/$PREFIX/allowed-mc.txt"
  "$MC_BIN" ls "$MC_ALIAS/$TARGET_BUCKET/$PREFIX/"
fi

echo "6. Prove unlisted bucket fails"
if [[ "$USE_AWS" == "1" ]]; then
  if "$AWS_BIN" --endpoint-url "$ENDPOINT" s3 ls "s3://$OTHER_BUCKET/" >"$unlisted_out" 2>&1; then
    cat "$unlisted_out" >&2
    echo "expected unlisted bucket to fail with aws, but it succeeded" >&2
    exit 1
  fi
fi
if [[ "$USE_MC" == "1" ]]; then
  if "$MC_BIN" ls "$MC_ALIAS/$OTHER_BUCKET/" >"$unlisted_out" 2>&1; then
    cat "$unlisted_out" >&2
    echo "expected unlisted bucket to fail with mc, but it succeeded" >&2
    exit 1
  fi
fi

echo "7. Set tiny per-key monthly budget"
echo "   This step requires the vh CLI context to have S3 gateway admin permission."
run_vh s3-gateway budget set-key "$ACCESS_KEY" \
  --monthly "$MONTHLY_LIMIT" \
  --mode enforce \
  --currency USD

if [[ "$REMOTE_CREATED" == "1" ]]; then
  echo "8. Run remote-backed operation until budget denial"
  denied=0
  for idx in $(seq 1 20); do
    if [[ "$AWS_AVAILABLE" == "1" ]]; then
      if ! "$AWS_BIN" --endpoint-url "$ENDPOINT" s3 cp "$tmp_file" "s3://$TARGET_BUCKET/$PREFIX/budget-$idx.txt" >"$budget_out" 2>&1; then
        cat "$budget_out"
        denied=1
        break
      fi
    elif ! "$MC_BIN" cp "$tmp_file" "$MC_ALIAS/$TARGET_BUCKET/$PREFIX/budget-$idx.txt" >"$budget_out" 2>&1; then
      cat "$budget_out"
      denied=1
      break
    fi
  done
  if [[ "$denied" != "1" ]]; then
    echo "remote-backed operation did not hit the tiny budget limit"
    if [[ "${S3_GATEWAY_SMOKE_REQUIRE_BUDGET_DENIAL:-0}" == "1" ]]; then
      exit 1
    fi
  fi
else
  echo "8. Skipping remote budget denial because no remote-cache bucket was created"
fi

echo "9. Show budget ledger/status"
run_vh s3-gateway budget status --key "$ACCESS_KEY" --json
run_vh s3-gateway budget ledger --key "$ACCESS_KEY" --limit 20 --json

echo "10. Revoke credential"
if [[ "$KEEP_RESOURCES" != "1" ]]; then
  run_vh s3-gateway creds revoke "$ACCESS_KEY"
  ACCESS_KEY=""
else
  echo "keeping credential due to --keep-resources"
fi
