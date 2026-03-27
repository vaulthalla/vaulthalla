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
