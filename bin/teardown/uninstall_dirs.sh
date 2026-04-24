echo "🗑️  Cleaning directories..."

PROGRAM_NAME=vaulthalla

sudo rm -f /etc/nginx/sites-enabled/vaulthalla
sudo rm -f /etc/nginx/sites-available/vaulthalla

for dir in /mnt /var/lib /var/log /run /etc /usr/share /usr/lib ; do
  sudo rm -rf "$dir/$PROGRAM_NAME"
done

sudo rm -rf "/var/lib/swtpm/$PROGRAM_NAME"
sudo rmdir /var/lib/swtpm >/dev/null 2>&1 || true
