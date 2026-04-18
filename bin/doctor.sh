#!/usr/bin/env bash
set -euo pipefail

PROJECT="vaulthalla"

BINDIR="/usr/bin"
CONFIG_DIR="/etc/$PROJECT"
DATA_DIR="/usr/share/$PROJECT"
RUNTIME_DIR="/var/run/$PROJECT"
STATE_DIR="/var/lib/$PROJECT"
LOG_DIR="/var/log/$PROJECT"
MOUNT_DIR="/mnt/$PROJECT"

echo "🧠 Vaulthalla Doctor — System Inspection"
echo "========================================"
echo

check_path() {
  local p="$1"

  if [[ -L "$p" ]]; then
    printf "🔗 %s\n" "$p"
    stat -Lc "   → %N
   type: %F
   perms: %a (%A)
   owner: %U:%G" "$p"
  elif [[ -e "$p" ]]; then
    printf "📁 %s\n" "$p"
    stat -Lc "   type: %F
   perms: %a (%A)
   owner: %U:%G" "$p"
  else
    printf "❌ %s (missing)\n" "$p"
  fi

  echo
}

echo "---- 📦 Binaries ----"
check_path "$BINDIR/vaulthalla-server"
check_path "$BINDIR/vaulthalla-cli"
check_path "$BINDIR/vaulthalla"
check_path "$BINDIR/vh"

echo "---- ⚙️ Config ----"
check_path "$CONFIG_DIR"
check_path "$CONFIG_DIR/config.yaml"
check_path "$CONFIG_DIR/vaulthalla.env"

echo "---- 🧱 Runtime ----"
check_path "$RUNTIME_DIR"
check_path "$STATE_DIR"
check_path "$LOG_DIR"
check_path "$MOUNT_DIR"

echo "---- 🗄️ Data ----"
check_path "$DATA_DIR"
check_path "$DATA_DIR/psql"

echo "---- 👤 User ----"
if id "$PROJECT" &>/dev/null; then
  id "$PROJECT"
else
  echo "❌ user '$PROJECT' missing"
fi

if getent group "$PROJECT" &>/dev/null; then
  getent group "$PROJECT"
else
  echo "❌ group '$PROJECT' missing"
fi

echo
echo "---- 🔧 systemd ----"

for unit in \
  vaulthalla.service \
  vaulthalla-cli.service \
  vaulthalla-cli.socket \
  vaulthalla-web.service
do
  echo "▶ $unit"

  systemctl status "$unit" --no-pager --lines=2 2>/dev/null || echo "   (not loaded)"

  echo "   ExecStart:"
  systemctl show "$unit" -p ExecStart 2>/dev/null | sed "s/^/   /" || true

  echo
done

echo "---- 🌍 Env Sanity ----"

if [[ -f "$CONFIG_DIR/vaulthalla.env" ]]; then
  echo "📄 $CONFIG_DIR/vaulthalla.env"
  nl -ba "$CONFIG_DIR/vaulthalla.env" | sed "s/^/   /"

  echo
  echo "⚠️  suspicious lines:"
  grep -nE "^[[:space:]]*export[[:space:]]+|[[:space:]]+=[[:space:]]*" "$CONFIG_DIR/vaulthalla.env" || echo "   none"
else
  echo "❌ env file missing"
fi

echo
echo "---- 🔍 Process Reality ----"

echo "Running vaulthalla processes:"
pgrep -fa vaulthalla || echo "   none"

echo
echo "Mounted FUSE:"
mount | grep vaulthalla || echo "   none"

echo
echo "========================================"
echo "🧙 Diagnosis complete."
