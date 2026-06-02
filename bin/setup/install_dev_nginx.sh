#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

source "$ROOT_DIR/bin/lib/dev_mode.sh"

env_flag_enabled() {
  case "${1:-}" in
    1|true|TRUE|yes|YES|on|ON)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

if ! vh_is_dev_mode; then
  exit 0
fi

if env_flag_enabled "${VH_SKIP_DEV_NGINX:-}"; then
  echo "Skipping dev nginx setup (VH_SKIP_DEV_NGINX=${VH_SKIP_DEV_NGINX})."
  exit 0
fi

: "${VH_DEV_WEB_DOMAIN:=vaulthalla.dev}"
: "${VH_DEV_S3_DOMAIN:=s3.vaulthalla.dev}"
: "${VH_DEV_CLOUDFLARE_CREDENTIALS_DIR:=/etc/vaulthalla/certbot}"
: "${VH_DEV_CLOUDFLARE_CREDENTIAL_FILENAME:=cloudflare.ini}"
: "${VH_DEV_CLOUDFLARE_CREDENTIALS:=${VH_DEV_CLOUDFLARE_CREDENTIALS_DIR}/${VH_DEV_CLOUDFLARE_CREDENTIAL_FILENAME}}"

if [[ -f "./${VH_DEV_CLOUDFLARE_CREDENTIAL_FILENAME}" ]]; then
  sudo mkdir -p "${VH_DEV_CLOUDFLARE_CREDENTIALS_DIR}"
  sudo cp "./${VH_DEV_CLOUDFLARE_CREDENTIAL_FILENAME}" "${VH_DEV_CLOUDFLARE_CREDENTIALS_DIR}"
  sudo chmod 600 "${VH_DEV_CLOUDFLARE_CREDENTIALS}"
fi

if [[ ! -f "$VH_DEV_CLOUDFLARE_CREDENTIALS" ]]; then
  cat >&2 <<EOF
Missing Cloudflare credentials for dev nginx setup: $VH_DEV_CLOUDFLARE_CREDENTIALS

Create a 0600 Certbot Cloudflare credentials file or rerun with VH_SKIP_DEV_NGINX=1.
EOF
  exit 1
fi

LIFECYCLE="${VH_LIFECYCLE_BIN:-/usr/lib/vaulthalla/lifecycle}"
if [[ ! -x "$LIFECYCLE" ]]; then
  LIFECYCLE="$ROOT_DIR/deploy/lifecycle/main.py"
fi

echo "Configuring dev nginx for $VH_DEV_WEB_DOMAIN and $VH_DEV_S3_DOMAIN..."
"$LIFECYCLE" setup nginx \
  --domain "$VH_DEV_WEB_DOMAIN" \
  --s3-domain "$VH_DEV_S3_DOMAIN" \
  --certbot-dns-cloudflare \
  --cloudflare-credentials "$VH_DEV_CLOUDFLARE_CREDENTIALS"
