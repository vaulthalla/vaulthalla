#!/usr/bin/env bash
set -euo pipefail

echo "🗑️  Removing Vaulthalla systemd services..."

UNITS=(
  vaulthalla-web.service
  vaulthalla-cli.service
  vaulthalla-cli.socket
  vaulthalla.service
  vaulthalla-swtpm.service
)

unit_exists() {
  local unit="$1"
  systemctl list-unit-files "$unit" >/dev/null 2>&1 || systemctl status "$unit" >/dev/null 2>&1
}

safe_systemctl() {
  local action="$1" unit="$2"
  if ! unit_exists "$unit"; then
    echo "✅ $unit not installed/loaded."
    return 0
  fi

  echo "• systemctl $action $unit"
  if ! sudo systemctl "$action" "$unit"; then
    echo "⚠️  systemctl $action $unit failed; continuing with exact-unit cleanup only."
  fi
}

for unit in "${UNITS[@]}"; do
  safe_systemctl stop "$unit"
done

for unit in "${UNITS[@]}"; do
  safe_systemctl disable "$unit"
done

echo "🧹 Removing exact Vaulthalla unit files and overrides..."
sudo rm -rf /etc/systemd/system/vaulthalla.service.d

for unit in "${UNITS[@]}"; do
  sudo rm -f "/etc/systemd/system/$unit"
  sudo rm -f "/lib/systemd/system/$unit"
  sudo rm -f "/usr/lib/systemd/system/$unit"
done

sudo systemctl daemon-reload

for unit in "${UNITS[@]}"; do
  if ! sudo systemctl reset-failed "$unit" >/dev/null 2>&1; then
    echo "⚠️  Could not reset failed state for $unit."
  fi
done

echo "✅ Exact Vaulthalla systemd units purged."
