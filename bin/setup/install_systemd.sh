source "$(dirname "$0")/../lib/dev_mode.sh"

echo "🛠️  Installing systemd service..."

# uncomment the EnvironmentFile line in the service file
sudo install -d -m 0755 /etc/systemd/system/vaulthalla.service.d

if [ -f /etc/vaulthalla/vaulthalla.env ]; then
  printf '[Service]\nEnvironmentFile=/etc/vaulthalla/vaulthalla.env\n' \
  | sudo tee /etc/systemd/system/vaulthalla.service.d/override.conf >/dev/null
fi

sudo systemctl daemon-reload
sudo systemctl enable --now vaulthalla.service
sudo systemctl enable --now vaulthalla-cli.socket
sudo systemctl enable --now vaulthalla-cli.service

echo ""
echo "🏁 Vaulthalla installed successfully!"

sudo journalctl -f -u vaulthalla
