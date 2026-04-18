#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

: "${VH_USER:=vaulthalla}"
: "${VH_GROUP:=vaulthalla}"
: "${PREFIX:=/usr}"
: "${BINDIR:=$PREFIX/bin}"
: "${SYSCONFDIR:=/etc}"
: "${DATADIR:=$PREFIX/share}"
: "${SYSTEMD_UNIT_DIR:=/etc/systemd/system}"

PROJECT_NAME="vaulthalla"

CONFIG_DIR="$SYSCONFDIR/$PROJECT_NAME"
DATA_DIR="$DATADIR/$PROJECT_NAME"
RUNTIME_DIR="/run/$PROJECT_NAME"
STATE_DIR="/var/lib/$PROJECT_NAME"
LOG_DIR="/var/log/$PROJECT_NAME"
MOUNT_DIR="/mnt/$PROJECT_NAME"

install_dir() {
  local dir="$1"
  local mode="$2"
  sudo mkdir -p "$dir"
  sudo chmod "$mode" "$dir"
  sudo chown "$VH_USER:$VH_GROUP" "$dir"
}

install_file() {
  local src="$1"
  local dst="$2"
  local mode="${3:-0644}"

  sudo mkdir -p "$(dirname "$dst")"
  sudo install -m "$mode" "$src" "$dst"
}

echo "📁 Installing Vaulthalla runtime/data directories..."

install_dir "$RUNTIME_DIR" 750
install_dir "$STATE_DIR" 700
install_dir "$LOG_DIR" 750
install_dir "$MOUNT_DIR" 755

echo "🔗 Installing CLI symlinks..."
sudo ln -sf "$BINDIR/vaulthalla-cli" "$BINDIR/vaulthalla"
sudo ln -sf "$BINDIR/vaulthalla-cli" "$BINDIR/vh"

echo "📦 Installing shared data/config payloads..."

if [[ -f "$ROOT_DIR/deploy/vaulthalla.env" ]]; then
  install_file "$ROOT_DIR/deploy/vaulthalla.env" "$CONFIG_DIR/vaulthalla.env" 0640
  sudo chown "$VH_USER:$VH_GROUP" "$CONFIG_DIR/vaulthalla.env"
fi

if [[ -f "$ROOT_DIR/config.yaml" ]]; then
  install_file "$ROOT_DIR/config.yaml" "$CONFIG_DIR/config.yaml" 0640
  sudo chown "$VH_USER:$VH_GROUP" "$CONFIG_DIR/config.yaml"
else
  install_file "$ROOT_DIR/deploy/config/config.yaml" "$CONFIG_DIR/config.yaml" 0640
  sudo chown "$VH_USER:$VH_GROUP" "$CONFIG_DIR/config.yaml"
fi

install_file "$ROOT_DIR/deploy/config/config_template.yaml.in" "$CONFIG_DIR/config_template.yaml.in" 0640
sudo chown "$VH_USER:$VH_GROUP" "$CONFIG_DIR/config_template.yaml.in"

echo "🗄️  Installing SQL assets..."
if [[ -d "$ROOT_DIR/deploy/psql" ]]; then
  sudo mkdir -p "$DATA_DIR"
  sudo rsync -a "$ROOT_DIR/deploy/psql/" "$DATA_DIR/psql/"
fi

echo "⚙️  Installing systemd socket payload..."
if [[ -f "$ROOT_DIR/deploy/systemd/vaulthalla-cli.socket" ]]; then
  install_file \
    "$ROOT_DIR/deploy/systemd/vaulthalla-cli.socket" \
    "$SYSTEMD_UNIT_DIR/vaulthalla-cli.socket" \
    0644
fi

# Historical Meson experiments kept here for reference only:
#
# license_files: ['LICENSE', 'debian/copyright']
# install_data('LICENSE', install_dir: license_dir)
# install_data('debian/copyright', install_dir: license_dir, rename: 'copyright')
# install_data('debian/tmpfiles.d/vaulthalla.conf', install_dir: ...)
# install_data('debian/vaulthalla.udev', install_dir: ..., rename: '60-vaulthalla-tpm.rules')

echo "✅ Directory/data installation complete."
