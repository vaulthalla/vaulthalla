#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

source "$ROOT_DIR/bin/lib/dev_mode.sh"

: "${PREFIX:=/usr}"
: "${BINDIR:=$PREFIX/bin}"
: "${SYSTEMD_UNIT_DIR:=/etc/systemd/system}"
: "${CONFIG_DIR:=/etc/vaulthalla}"
: "${VH_USER:=vaulthalla}"
: "${VH_GROUP:=vaulthalla}"

SWTPM_BASE_DIR="/var/lib/swtpm"
SWTPM_STATE_DIR="$SWTPM_BASE_DIR/vaulthalla"
TPM_BACKEND_DROPIN="$SYSTEMD_UNIT_DIR/vaulthalla.service.d/tpm-backend.conf"
TPM_BACKEND_MODE="hardware"

render_unit() {
  local input="$1"
  local output="$2"

  [[ -f "$input" ]] || {
    echo "❗️ Missing systemd template: $input"
    exit 1
  }

  sudo mkdir -p "$(dirname "$output")"
  sed "s|@BINDIR@|$BINDIR|g" "$input" | sudo tee "$output" >/dev/null
  sudo chmod 0644 "$output"
}

install_unit_file() {
  local input="$1"
  local output="$2"

  [[ -f "$input" ]] || {
    echo "❗️ Missing systemd unit: $input"
    exit 1
  }

  sudo install -m 0644 "$input" "$output"
}

has_hardware_tpm() {
  [[ -c /dev/tpmrm0 || -c /dev/tpm0 ]]
}

assert_swtpm_listening() {
  if ! command -v ss >/dev/null 2>&1; then
    return 0
  fi
  ss -H -ltn '( sport = :2321 )' 2>/dev/null | grep -q '127.0.0.1:2321'
}

configure_tpm_backend() {
  if has_hardware_tpm; then
    TPM_BACKEND_MODE="hardware"
    echo "🔐 Hardware TPM detected; using hardware TPM backend."
    sudo rm -f "$TPM_BACKEND_DROPIN"
    sudo systemctl disable --now vaulthalla-swtpm.service >/dev/null 2>&1 || true
    return 0
  fi

  TPM_BACKEND_MODE="swtpm"
  echo "🔐 No hardware TPM detected; provisioning managed swtpm backend."

  if ! command -v swtpm >/dev/null 2>&1; then
    echo "❗️ swtpm binary is required when no hardware TPM is present."
    echo "   Install with: sudo apt install swtpm swtpm-tools"
    exit 1
  fi

  sudo install -d -m 0755 "$SWTPM_BASE_DIR"
  sudo install -d -m 0700 -o "$VH_USER" -g "$VH_GROUP" "$SWTPM_STATE_DIR"
  sudo install -d -m 0755 "$(dirname "$TPM_BACKEND_DROPIN")"
  printf '[Service]\nEnvironment=TSS2_TCTI=swtpm:host=127.0.0.1,port=2321\n' \
    | sudo tee "$TPM_BACKEND_DROPIN" >/dev/null
  sudo chmod 0644 "$TPM_BACKEND_DROPIN"
}

echo "🛠️  Installing Vaulthalla systemd units..."

render_unit \
  "$ROOT_DIR/deploy/systemd/vaulthalla.service.in" \
  "$SYSTEMD_UNIT_DIR/vaulthalla.service"

render_unit \
  "$ROOT_DIR/deploy/systemd/vaulthalla-cli.service.in" \
  "$SYSTEMD_UNIT_DIR/vaulthalla-cli.service"

render_unit \
  "$ROOT_DIR/deploy/systemd/vaulthalla-web.service.in" \
  "$SYSTEMD_UNIT_DIR/vaulthalla-web.service"

render_unit \
  "$ROOT_DIR/deploy/systemd/vaulthalla-swtpm.service.in" \
  "$SYSTEMD_UNIT_DIR/vaulthalla-swtpm.service"

install_unit_file \
  "$ROOT_DIR/deploy/systemd/vaulthalla-cli.socket" \
  "$SYSTEMD_UNIT_DIR/vaulthalla-cli.socket"

echo "🧩 Installing service overrides..."
sudo install -d -m 0755 "$SYSTEMD_UNIT_DIR/vaulthalla.service.d"

if [[ -f "$CONFIG_DIR/vaulthalla.env" ]]; then
  printf '[Service]\nEnvironmentFile=%s/vaulthalla.env\n' "$CONFIG_DIR" \
    | sudo tee "$SYSTEMD_UNIT_DIR/vaulthalla.service.d/override.conf" >/dev/null
else
  sudo rm -f "$SYSTEMD_UNIT_DIR/vaulthalla.service.d/override.conf"
fi

configure_tpm_backend

sudo systemctl daemon-reload

echo "🚀 Enabling Vaulthalla services..."
if [[ "$TPM_BACKEND_MODE" == "swtpm" ]]; then
  sudo systemctl enable --now vaulthalla-swtpm.service
  if ! sudo systemctl --quiet is-active vaulthalla-swtpm.service; then
    echo "❗️ vaulthalla-swtpm.service failed to become active."
    echo "   Inspect with: sudo journalctl -u vaulthalla-swtpm.service"
    exit 1
  fi
  if ! assert_swtpm_listening; then
    echo "❗️ swtpm expected listener not detected on 127.0.0.1:2321."
    echo "   Inspect with: sudo journalctl -u vaulthalla-swtpm.service"
    exit 1
  fi
fi
sudo systemctl enable --now vaulthalla.service
sudo systemctl enable --now vaulthalla-cli.socket
sudo systemctl enable --now vaulthalla-cli.service
sudo systemctl enable --now vaulthalla-web.service

echo
echo "🏁 Vaulthalla installed successfully!"

if [[ "${VH_FOLLOW_LOGS:-0}" == "1" ]]; then
  echo "📜 Tailing vaulthalla.service logs..."
  sudo journalctl -f -u vaulthalla.service
else
  echo "📜 Recent vaulthalla.service logs:"
  sudo journalctl -n 25 --no-pager -u vaulthalla.service
fi
