#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

source "$ROOT_DIR/bin/lib/dev_mode.sh"

: "${PREFIX:=/usr}"
: "${BINDIR:=$PREFIX/bin}"
: "${SYSTEMD_UNIT_DIR:=/etc/systemd/system}"
: "${CONFIG_DIR:=/etc/vaulthalla}"

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

sudo systemctl daemon-reload

echo "🚀 Enabling Vaulthalla services..."
sudo systemctl enable --now vaulthalla.service
sudo systemctl enable --now vaulthalla-cli.socket
sudo systemctl enable --now vaulthalla-cli.service
sudo systemctl enable --now vaulthalla-web.service

echo
echo "🏁 Vaulthalla installed successfully!"
echo "📜 Tailing vaulthalla.service logs..."
sudo journalctl -f -u vaulthalla.service
