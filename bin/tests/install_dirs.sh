#!/usr/bin/env bash
set -euo pipefail

: "${VH_USER:=vaulthalla}"
: "${VH_GROUP:=vaulthalla}"

install_dir() {
  local dir="$1"
  local mode="$2"

  sudo mkdir -p "$dir"
  sudo chmod "$mode" "$dir"
  sudo chown "$VH_USER:$VH_GROUP" "$dir"
}

echo "🧪 Installing Vaulthalla integration test directories..."

install_dir "/tmp/vh_mount" 755
install_dir "/tmp/vh_backing" 755
install_dir "/tmp/vh_runtime" 755

echo "✅ Integration test directories installed."
