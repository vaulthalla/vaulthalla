safe_rm_dir() {
  local path="$1"

  [[ -n "$path" ]] || { echo "Refusing to remove empty path"; return 1; }
  [[ "$path" != "/" ]] || { echo "Refusing to remove /"; return 1; }

  if command -v findmnt >/dev/null 2>&1 && findmnt -rn -M "$path" >/dev/null 2>&1; then
    echo "Refusing to remove mounted path: $path"
    return 1
  fi

  sudo rm -rf --one-file-system "$path"
}

echo "🗑️  Cleaning directories..."

PROGRAM_NAME=vaulthalla

for dir in /mnt /var/lib /var/log /run /etc /usr/share; do
  safe_rm_dir "$dir/$PROGRAM_NAME"
done

safe_rm_dir /tmp/vh_mount
safe_rm_dir /tmp/vh_backing
