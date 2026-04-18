#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

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

# First install the normal product dirs/data setup.
"$ROOT_DIR/bin/setup/install_dirs.sh"

# Then install test-only mount/backing dirs.
install_dir "/tmp/vh_mount" 755
install_dir "/tmp/vh_backing" 755

echo "✅ Integration test directories installed."
