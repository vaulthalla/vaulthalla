#!/usr/bin/env bash
set -uo pipefail

WEB_URL="${VAULTHALLA_E2E_BASE_URL:-http://127.0.0.1:3000}"
GATEWAY_ENDPOINT="${S3_GATEWAY_ENDPOINT:-http://127.0.0.1:39000}"
PREFIX="${S3_GATEWAY_E2E_PREFIX:-s3-gateway-e2e/$(date -u +%Y%m%dT%H%M%SZ)-$$}"
REQUIRE_REMOTE=0

usage() {
  cat <<'USAGE'
Usage: tools/smoke/s3_gateway_e2e.sh [options]

Options:
  --require-remote   Require the R2/S3-backed smoke pass.
  --prefix <prefix>  Prefix for smoke data. Defaults to s3-gateway-e2e/<timestamp>-<pid>.
  -h, --help         Show this help.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --require-remote)
      REQUIRE_REMOTE=1
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

reachable() {
  local url="$1"
  curl --connect-timeout 3 -sS -o /dev/null "$url" >/dev/null 2>&1
}

remote_configured() {
  [[ -n "${S3_GATEWAY_SMOKE_API_KEY:-}" && -n "${S3_GATEWAY_SMOKE_UPSTREAM_BUCKET:-}" ]] && return 0
  [[ -n "${S3_GATEWAY_SMOKE_UPSTREAM_BUCKET:-}" &&
     -n "${S3_GATEWAY_SMOKE_UPSTREAM_ACCESS_KEY:-}" &&
     -n "${S3_GATEWAY_SMOKE_UPSTREAM_SECRET_KEY:-}" &&
     -n "${S3_GATEWAY_SMOKE_UPSTREAM_ENDPOINT:-}" ]] && return 0
  [[ -n "${VAULTHALLA_TEST_R2_BUCKET:-}" &&
     -n "${VAULTHALLA_TEST_R2_ACCESS_KEY:-}" &&
     -n "${VAULTHALLA_TEST_R2_SECRET_ACCESS_KEY:-}" &&
     -n "${VAULTHALLA_TEST_R2_ENDPOINT:-}" ]]
}

WEB_STATUS=not-run
LOCAL_STATUS=not-run
REMOTE_STATUS=not-run
FAILED=0

if ! reachable "$WEB_URL"; then
  echo "web stack is not reachable at $WEB_URL" >&2
  WEB_STATUS=unreachable
  FAILED=1
else
  if VAULTHALLA_E2E_BASE_URL="$WEB_URL" pnpm --dir web run test:e2e:s3-gateway; then
    WEB_STATUS=pass
  else
    WEB_STATUS=fail
    FAILED=1
  fi
fi

if ! reachable "$GATEWAY_ENDPOINT"; then
  echo "S3 gateway is not reachable at $GATEWAY_ENDPOINT" >&2
  LOCAL_STATUS=unreachable
  FAILED=1
else
  if tools/smoke/s3_gateway_scoped_budget_smoke.sh --local-only --prefix "$PREFIX/local"; then
    LOCAL_STATUS=pass
  else
    LOCAL_STATUS=fail
    FAILED=1
  fi
fi

if [[ "$REQUIRE_REMOTE" == "1" ]] || remote_configured; then
  if tools/smoke/s3_gateway_scoped_budget_smoke.sh --require-remote --prefix "$PREFIX/remote"; then
    REMOTE_STATUS=pass
  else
    REMOTE_STATUS=fail
    FAILED=1
    echo "REMOTE SMOKE FAILED; check cleanup output for prefix: $PREFIX/remote" >&2
  fi
else
  REMOTE_STATUS=skipped-no-remote-config
fi

echo
if [[ "$FAILED" == "0" ]]; then
  echo "S3 gateway E2E summary: PASS"
else
  echo "S3 gateway E2E summary: FAIL"
fi
echo "  web: $WEB_STATUS ($WEB_URL)"
echo "  local smoke: $LOCAL_STATUS ($GATEWAY_ENDPOINT)"
echo "  remote smoke: $REMOTE_STATUS"
echo "  prefix: $PREFIX"

exit "$FAILED"
