#!/usr/bin/env bash
# Nuke only the FUSE mount at /tmp/vh_mount.
# No systemd. No broad process killing. No killing every process using the mount.

set -euo pipefail

MOUNT="/tmp/vh_mount"

log(){ printf '[nuke-vh-mount] %s\n' "$*"; }

canon_mount() {
  readlink -f "$MOUNT" 2>/dev/null || echo "$MOUNT"
}

is_mounted() {
  local mp
  mp="$(canon_mount)"

  if command -v findmnt >/dev/null 2>&1; then
    findmnt -rn -M "$mp" >/dev/null 2>&1 && return 0
  fi

  grep -Fq " $mp " /proc/self/mountinfo 2>/dev/null
}

mount_pid() {
  local mp src
  mp="$(canon_mount)"

  if command -v findmnt >/dev/null 2>&1; then
    src="$(findmnt -rn -M "$mp" -o SOURCE 2>/dev/null || true)"
  else
    src="$(mount | awk -v m="$mp" '$3 == m { print $1; exit }' || true)"
  fi

  # Typical FUSE source looks like:
  #   vaulthalla-fuse[12345]
  #   /dev/fuse
  #   somefs
  if [[ "$src" =~ \[([0-9]+)\]$ ]]; then
    printf '%s\n' "${BASH_REMATCH[1]}"
    return 0
  fi

  return 1
}

kill_pid_tree() {
  local pid="$1"
  [[ -n "${pid:-}" ]] || return 0

  if ! kill -0 "$pid" 2>/dev/null; then
    return 0
  fi

  if command -v pgrep >/dev/null 2>&1; then
    local kids=""
    kids="$(pgrep -P "$pid" 2>/dev/null | tr '\n' ' ' || true)"
    if [[ -n "${kids//[[:space:]]/}" ]]; then
      for child in $kids; do
        kill_pid_tree "$child"
      done
    fi
  fi

  log "Killing PID $pid"
  sudo kill -9 "$pid" 2>/dev/null || true
}

unmount_path() {
  local mp
  mp="$(canon_mount)"

  if ! is_mounted; then
    log "$mp is not mounted"
    return 0
  fi

  if command -v fusermount3 >/dev/null 2>&1; then
    log "Unmount: sudo fusermount3 -uz $mp"
    sudo fusermount3 -uz "$mp" 2>/dev/null || true
  elif command -v fusermount >/dev/null 2>&1; then
    log "Unmount: sudo fusermount -uz $mp"
    sudo fusermount -uz "$mp" 2>/dev/null || true
  fi

  if is_mounted; then
    log "Unmount: sudo umount -l $mp"
    sudo umount -l "$mp" 2>/dev/null || true
  fi
}

remove_mount_dir() {
  local mp
  mp="$(canon_mount)"

  if is_mounted; then
    log "❌ $mp is still mounted; refusing to remove live mountpoint"
    return 1
  fi

  if [[ -e "$mp" ]]; then
    log "Removing $mp"
    sudo rm -rf --one-file-system "$mp"
  else
    log "$mp does not exist"
  fi
}

main() {
  local mp pid
  mp="$(canon_mount)"

  log "Target: $mp"

  if is_mounted; then
    if pid="$(mount_pid)"; then
      log "Mount owner PID: $pid"
      kill_pid_tree "$pid"
    else
      log "Could not determine mount owner PID from mount source"
    fi
  fi

  unmount_path
  remove_mount_dir

  if is_mounted; then
    log "❌ Mount still present at $mp"
    exit 1
  fi

  if [[ -e "$mp" ]]; then
    log "❌ Path still exists: $mp"
    exit 1
  fi

  log "✅ /tmp/vh_mount annihilated."
}

main "$@"
