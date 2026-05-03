#!/usr/bin/env bash
set -euo pipefail

echo "🗑️  Cleaning Vaulthalla directories..."

PROGRAM_NAME=vaulthalla

is_mounted_path() {
  local path="$1"
  if command -v findmnt >/dev/null 2>&1; then
    findmnt -rn -M "$path" >/dev/null 2>&1
    return $?
  fi
  grep -Fq " $(readlink -f "$path" 2>/dev/null || echo "$path") " /proc/self/mountinfo 2>/dev/null
}

safe_rm_tree() {
  local path="$1"
  case "$path" in
    /mnt/vaulthalla|/var/lib/vaulthalla|/var/log/vaulthalla|/run/vaulthalla|/etc/vaulthalla|/usr/share/vaulthalla|/usr/lib/vaulthalla|/var/lib/swtpm/vaulthalla)
      ;;
    *)
      echo "Refusing to remove unexpected path: $path" >&2
      return 1
      ;;
  esac

  if [[ ! -e "$path" ]]; then
    return 0
  fi

  if is_mounted_path "$path"; then
    echo "Refusing to remove mounted path: $path" >&2
    return 1
  fi

  sudo rm -rf --one-file-system "$path"
}

sudo rm -f /etc/nginx/sites-enabled/vaulthalla
sudo rm -f /etc/nginx/sites-available/vaulthalla

for dir in /mnt /var/lib /var/log /run /etc /usr/share /usr/lib ; do
  safe_rm_tree "$dir/$PROGRAM_NAME"
done

safe_rm_tree "/var/lib/swtpm/$PROGRAM_NAME"
if ! sudo rmdir /var/lib/swtpm >/dev/null 2>&1; then
  echo "ℹ️  /var/lib/swtpm not empty or not removable; leaving it in place."
fi
