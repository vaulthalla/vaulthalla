echo "🗑️  Cleaning directories..."

PROGRAM_NAME=vaulthalla

for dir in /mnt /var/lib /var/log /run /etc /usr/share ; do
  sudo rm -rf "$dir/$PROGRAM_NAME"
done
