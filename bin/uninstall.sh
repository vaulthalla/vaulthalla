#!/usr/bin/env bash
set -euo pipefail

echo "🗡️  Initiating Vaulthalla uninstallation sequence..."

DEV_MODE=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--dev)
            DEV_MODE=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

if mountpoint -q /mnt/vaulthalla; then
  echo "Unmounting stale FUSE mount..."
  sudo umount -l /mnt/vaulthalla || sudo fusermount3 -u /mnt/vaulthalla
fi

# === 1) Stop and disable systemd service (if exists) ===
echo "🗑️  Removing systemd services..."
sudo systemctl stop vaulthalla.service || true
sudo systemctl stop vaulthalla-cli.service || true
sudo systemctl stop vaulthalla-cli.socket || true
sudo systemctl disable vaulthalla-cli.service || true
sudo systemctl disable vaulthalla-cli.socket || true
sudo systemctl disable vaulthalla.service || true

# Ensure the socket actually stopped
if systemctl is-active --quiet vaulthalla-cli.socket; then
    echo "⚠️  vaulthalla-cli.socket is still active, attempting to stop again..."
    sudo systemctl kill vaulthalla-cli.socket || true
fi

# === Remove all Vaulthalla systemd units ===
echo "🧹 Removing all vaulthalla systemd units..."

# Stop anything still running (don’t care if it fails)
sudo systemctl stop 'vaulthalla*' 2>/dev/null || true

# Disable all units
sudo systemctl disable 'vaulthalla*' 2>/dev/null || true

# Remove override directories
sudo rm -rf /etc/systemd/system/vaulthalla*.service.d

# Remove units from /etc (user-installed overrides)
sudo rm -f /etc/systemd/system/vaulthalla*

# Remove units from /lib (package-installed units)
sudo rm -f /lib/systemd/system/vaulthalla*

# Reload systemd so it forgets they ever existed
sudo systemctl daemon-reload

# Clear failed state cache (important for restart loops like you had)
sudo systemctl reset-failed

echo "✅ Vaulthalla systemd units fully purged."


# === 2) Remove binaries ===
echo "🧹 Removing installed binaries..."
sudo rm -f /usr/local/bin/vaulthalla*
sudo rm -f /usr/bin/vaulthalla*

# === 3) Remove runtime and config dirs ===
echo "🗑️  Cleaning directories..."

sudo rm -rf /mnt/vaulthalla
sudo rm -rf /var/lib/vaulthalla
sudo rm -rf /var/log/vaulthalla
sudo rm -rf /run/vaulthalla
sudo rm -rf /etc/vaulthalla
sudo rm -rf /usr/share/vaulthalla

# === 4) Remove system user ===
if id vaulthalla &>/dev/null; then
    echo "👤 Removing system user 'vaulthalla'..."
    sudo userdel -r vaulthalla || echo "⚠️  Could not delete home or user not removable"
else
    echo "✅ System user 'vaulthalla' not present."
fi

# Remove 'vaulthalla' group if it exists
if getent group vaulthalla > /dev/null; then
    echo "👥 Removing 'vaulthalla' group..."
    sudo groupdel vaulthalla || echo "⚠️  Could not delete group 'vaulthalla' or group not removable"
else
    echo "✅ 'vaulthalla' group not present."
fi

# === 5) Drop PostgreSQL DB and user ===
echo

if ! command -v psql >/dev/null 2>&1 || ! id -u postgres >/dev/null 2>&1; then
    echo "🛡️  PostgreSQL not installed or 'postgres' user missing. Skipping database cleanup."
else
    if [[ "$DEV_MODE" == true ]]; then
        echo "⚠️  [DEV_MODE] Dropping PostgreSQL DB and user 'vaulthalla'..."
        sudo -u postgres psql <<EOF
REVOKE CONNECT ON DATABASE vaulthalla FROM public;
SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE datname = 'vaulthalla';
DROP DATABASE IF EXISTS vaulthalla;
DROP DATABASE IF EXISTS vh_cli_test;
DROP ROLE IF EXISTS vaulthalla;
EOF
    else
        read -p "⚠️  Drop PostgreSQL database and user 'vaulthalla'? [y/N]: " confirm
        if [[ "$confirm" =~ ^[Yy]$ ]]; then
            echo "🧨 Dropping PostgreSQL DB and user..."
            sudo -u postgres psql <<EOF
REVOKE CONNECT ON DATABASE vaulthalla FROM public;
SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE datname = 'vaulthalla';
DROP DATABASE IF EXISTS vaulthalla;
DROP ROLE IF EXISTS vaulthalla;
EOF
        else
            echo "🛡️  Skipping database deletion. You can do it manually with psql if needed."
        fi
    fi
fi

# === 6) Uninstall Conan and Meson ===
if [[ "$DEV_MODE" == true ]]; then
    echo
    echo "🚫 Skipping Meson uninstall in DEV_MODE."
else
    echo
    read -p "❓ Uninstall Meson build system and Ninja? [y/N]: " remove_meson
    if [[ "$remove_meson" == "y" || "$remove_meson" == "Y" ]]; then
        echo "🧨 Removing Meson and Ninja..."
        sudo apt remove -y meson ninja-build || echo "⚠️ Could not remove Meson/Ninja"
    else
        echo "✅ Meson left installed."
    fi
fi

# === Done ===
echo ""
echo "✅ Vaulthalla has been uninstalled."
echo "🧙‍♂️ If this was a test install, may your next deployment rise stronger from these ashes."
