#!/usr/bin/env bash
set -euo pipefail

VH_BIN="${VH_BIN:-vh}"
AWS_BIN="${AWS_BIN:-aws}"
MC_BIN="${MC_BIN:-mc}"
JQ_BIN="${JQ_BIN:-jq}"
ENDPOINT="${S3_GATEWAY_ENDPOINT:-http://127.0.0.1:39000}"
LOCAL_BUCKET="${S3_GATEWAY_SMOKE_LOCAL_BUCKET:-vh-smoke-local}"
OTHER_BUCKET="${S3_GATEWAY_SMOKE_OTHER_BUCKET:-vh-smoke-denied}"
REMOTE_BUCKET="${S3_GATEWAY_SMOKE_REMOTE_BUCKET:-vh-smoke-remote}"
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
CREATED_UPSTREAM_API_KEY=0

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

create_local_bucket() {
  local bucket="$1"
  if run_vh s3-gateway bucket create-local "$bucket"; then
    return 0
  fi
  echo "create-local failed for $bucket; continuing in case it already exists" >&2
}

cleanup() {
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
  rm -f "${tmp_file:-}" "${unlisted_out:-}" "${budget_out:-}"
}

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

trap cleanup EXIT

echo "1. Enable S3 gateway"
run_vh s3-gateway enable

echo "2. Create local buckets"
create_local_bucket "$LOCAL_BUCKET"
create_local_bucket "$OTHER_BUCKET"

TARGET_BUCKET="$LOCAL_BUCKET"
REMOTE_CREATED=0
if [[ -z "$UPSTREAM_API_KEY" && -n "$UPSTREAM_ACCESS_KEY" && -n "$UPSTREAM_SECRET_KEY" && -n "$UPSTREAM_ENDPOINT" && -n "$UPSTREAM_BUCKET" ]]; then
  echo "3a. Create upstream API key from smoke environment"
  if run_vh api-key create "$UPSTREAM_API_KEY_NAME" \
    --access "$UPSTREAM_ACCESS_KEY" \
    --secret "$UPSTREAM_SECRET_KEY" \
    --provider "$UPSTREAM_PROVIDER" \
    --endpoint "$UPSTREAM_ENDPOINT" \
    --region "$UPSTREAM_REGION"; then
    UPSTREAM_API_KEY="$UPSTREAM_API_KEY_NAME"
    CREATED_UPSTREAM_API_KEY=1
  else
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
  else
    echo "remote-cache create failed; continuing with local-only scope checks" >&2
  fi
else
  echo "3. Skipping remote-cache bucket; set S3_GATEWAY_SMOKE_API_KEY and S3_GATEWAY_SMOKE_UPSTREAM_BUCKET, or raw upstream env vars, to enable it"
fi

echo "4. Create scoped credential for $TARGET_BUCKET"
credential_json="$(run_vh s3-gateway creds create "$CRED_NAME" \
  --scope vault-allowlist \
  --vault "$TARGET_BUCKET" \
  --list --read --write --delete \
  --json)"
ACCESS_KEY="$(printf '%s\n' "$credential_json" | json_value '.access_key')"
SECRET_KEY="$(printf '%s\n' "$credential_json" | json_value '.secret_access_key')"

export AWS_ACCESS_KEY_ID="$ACCESS_KEY"
export AWS_SECRET_ACCESS_KEY="$SECRET_KEY"
export AWS_EC2_METADATA_DISABLED=true
export AWS_DEFAULT_REGION="${AWS_DEFAULT_REGION:-us-east-1}"

tmp_file="$(mktemp)"
unlisted_out="$(mktemp)"
budget_out="$(mktemp)"
printf 'vaulthalla scoped gateway smoke %s\n' "$(date -Iseconds)" >"$tmp_file"

echo "5. Prove allowed bucket works"
if [[ "$AWS_AVAILABLE" == "1" ]]; then
  "$AWS_BIN" --endpoint-url "$ENDPOINT" s3 cp "$tmp_file" "s3://$TARGET_BUCKET/allowed-aws.txt"
  "$AWS_BIN" --endpoint-url "$ENDPOINT" s3 ls "s3://$TARGET_BUCKET/"
fi
if [[ "$MC_AVAILABLE" == "1" ]]; then
  "$MC_BIN" alias set "$MC_ALIAS" "$ENDPOINT" "$ACCESS_KEY" "$SECRET_KEY" >/dev/null
  "$MC_BIN" cp "$tmp_file" "$MC_ALIAS/$TARGET_BUCKET/allowed-mc.txt"
  "$MC_BIN" ls "$MC_ALIAS/$TARGET_BUCKET/"
fi

echo "6. Prove unlisted bucket fails"
if [[ "$AWS_AVAILABLE" == "1" ]]; then
  if "$AWS_BIN" --endpoint-url "$ENDPOINT" s3 ls "s3://$OTHER_BUCKET/" >"$unlisted_out" 2>&1; then
    cat "$unlisted_out" >&2
    echo "expected unlisted bucket to fail with aws, but it succeeded" >&2
    exit 1
  fi
fi
if [[ "$MC_AVAILABLE" == "1" ]]; then
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
      if ! "$AWS_BIN" --endpoint-url "$ENDPOINT" s3 cp "$tmp_file" "s3://$TARGET_BUCKET/budget-$idx.txt" >"$budget_out" 2>&1; then
        cat "$budget_out"
        denied=1
        break
      fi
    elif ! "$MC_BIN" cp "$tmp_file" "$MC_ALIAS/$TARGET_BUCKET/budget-$idx.txt" >"$budget_out" 2>&1; then
      cat "$budget_out"
      denied=1
      break
    fi
  done
  if [[ "$denied" != "1" ]]; then
    echo "remote-backed operation did not hit the tiny budget limit" >&2
    exit 1
  fi
else
  echo "8. Skipping remote budget denial because no remote-cache bucket was created"
fi

echo "9. Show budget ledger/status"
run_vh s3-gateway budget status --key "$ACCESS_KEY" --json
run_vh s3-gateway budget ledger --key "$ACCESS_KEY" --limit 20 --json

echo "10. Revoke credential"
run_vh s3-gateway creds revoke "$ACCESS_KEY"
ACCESS_KEY=""
