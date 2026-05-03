#!/usr/bin/env bash
set -euo pipefail

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

safe_rm_dir /tmp/vh_mount
safe_rm_dir /tmp/vh_backing
safe_rm_dir /tmp/vh_runtime
