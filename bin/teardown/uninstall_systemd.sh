#!/usr/bin/env bash
set -euo pipefail

echo "🗑️  Removing Vaulthalla systemd services..."
SYSTEMCTL_TIMEOUT="${VH_SYSTEMCTL_STOP_TIMEOUT:-35s}"

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

run_systemctl() {
  if command -v timeout >/dev/null 2>&1; then
    sudo timeout "$SYSTEMCTL_TIMEOUT" systemctl "$@"
  else
    sudo systemctl "$@"
  fi
}

unit_deactivating() {
  local unit="$1"
  [[ "$(systemctl show -p ActiveState --value "$unit" 2>/dev/null || true)" == "deactivating" ]]
}

safe_systemctl() {
  local action="$1" unit="$2"
  if ! unit_exists "$unit"; then
    echo "✅ $unit not installed/loaded."
    return 0
  fi

  echo "• systemctl $action $unit"
  if ! run_systemctl "$action" "$unit"; then
    echo "⚠️  systemctl $action $unit failed; continuing with exact-unit cleanup only."
    return 1
  fi
  return 0
}

for unit in "${UNITS[@]}"; do
  safe_systemctl stop "$unit" || true
done

for unit in "${UNITS[@]}"; do
  safe_systemctl disable "$unit"
done

echo "🧹 Removing exact Vaulthalla unit files and overrides..."
if unit_deactivating vaulthalla.service; then
  echo "⚠️  vaulthalla.service is still deactivating; preserving drop-ins until the stop job clears."
else
  sudo rm -rf /etc/systemd/system/vaulthalla.service.d
fi

for unit in "${UNITS[@]}"; do
  if unit_deactivating "$unit"; then
    echo "⚠️  Preserving $unit files because systemd still reports it deactivating."
    continue
  fi
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
