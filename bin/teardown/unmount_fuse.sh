#!/usr/bin/env bash
# Nuke vaulthalla immediately. No grace, no waits. All privileged ops use sudo.

set -euo pipefail

UNIT="vaulthalla.service"
MOUNT="/mnt/vaulthalla"

log(){ printf '[nuke-vaulthalla] %s\n' "$*"; }

ancestor_pids() {
  local pid="$$"
  while [[ -n "${pid:-}" && "$pid" -gt 1 ]]; do
    printf '%s\n' "$pid"
    pid="$(ps -o ppid= -p "$pid" 2>/dev/null | tr -d '[:space:]' || true)"
  done
}

PROTECTED_PIDS="$(ancestor_pids)"

is_protected_pid() {
  local candidate="$1"
  grep -Fxq "$candidate" <<<"$PROTECTED_PIDS"
}

kill_matching_processes() {
  local pat="$1"
  local pids=()
  local pid=""

  while read -r pid; do
    [[ -n "$pid" ]] || continue
    if is_protected_pid "$pid"; then
      log "SKIP (pgrep:$pat): protected PID $pid"
      continue
    fi
    pids+=("$pid")
  done < <(sudo pgrep -f "$pat" 2>/dev/null || true)

  if [[ "${#pids[@]}" -gt 0 ]]; then
    log "KILL (pgrep:$pat): ${pids[*]}"
    sudo kill -9 "${pids[@]}" 2>/dev/null || true
  fi
}

is_mounted() {
  # Reliable mount detection: findmnt -T, then /proc/self/mountinfo
  if command -v findmnt >/dev/null 2>&1; then
    if findmnt -rn -T "$MOUNT" >/dev/null 2>&1; then return 0; fi
  fi
  local mp; mp="$(readlink -f "$MOUNT" 2>/dev/null || echo "$MOUNT")"
  grep -q " $mp " /proc/self/mountinfo 2>/dev/null
}

STATE="$(sudo systemctl show -p ActiveState --value "$UNIT" 2>/dev/null || echo unknown)"
PID="$(sudo systemctl show -p MainPID    --value "$UNIT" 2>/dev/null | awk '$1>0{print $1}')"
CG="$(sudo systemctl show -p ControlGroup --value "$UNIT" 2>/dev/null || true)"
log "State: $STATE  PID: ${PID:--}  CGroup: ${CG:-unknown}"

# 1) Instantly SIGKILL the entire unit cgroup
log "KILL (cgroup): sudo systemctl kill --kill-who=all --signal=KILL $UNIT"
sudo systemctl kill --kill-who=all --signal=KILL "$UNIT" || true

# 2) cgroup v2 guillotine (hard kill the whole cgroup)
if [[ -n "${CG:-}" && -e "/sys/fs/cgroup${CG}/cgroup.kill" ]]; then
  log "Guillotine: echo 1 > /sys/fs/cgroup${CG}/cgroup.kill"
  printf '1\n' | sudo tee "/sys/fs/cgroup${CG}/cgroup.kill" >/dev/null 2>&1 || true
fi

# 3) Belt & suspenders: kill obvious stragglers by pattern
for pat in 'vaulthalla-serv' 'vaulthalla' 'vaulthalla-fuse' 'fuse.vaulthalla'; do
  kill_matching_processes "$pat"
done

# If we can read the cgroup’s procs, double-tap them
if [[ -n "${CG:-}" && -r "/sys/fs/cgroup${CG}/cgroup.procs" ]]; then
  PROCS="$(sudo cat "/sys/fs/cgroup${CG}/cgroup.procs" 2>/dev/null || true)"
  if [[ -n "${PROCS//[[:space:]]/}" ]]; then
    log "KILL (cgroup.procs): $(echo "$PROCS" | tr '\n' ' ')"
    sed '/^\s*$/d' <<<"$PROCS" | xargs -r -n1 sudo kill -9 >/dev/null 2>&1 || true
  fi
fi

# 4) Smash the FUSE mount
if is_mounted; then
  if command -v fusermount3 >/dev/null 2>&1; then
    log "Unmount: sudo fusermount3 -uz $MOUNT"
    sudo fusermount3 -uz "$MOUNT" 2>/dev/null || true
  elif command -v fusermount >/dev/null 2>&1; then
    log "Unmount: sudo fusermount -uz $MOUNT"
    sudo fusermount -uz "$MOUNT" 2>/dev/null || true
  fi

  if is_mounted; then
#    if command -v fuser >/dev/null 2>&1; then
#      log "Killing users of $MOUNT: sudo fuser -km $MOUNT"
#      sudo fuser -km "$MOUNT" 2>/dev/null || true
#    fi
    log "Unmount: sudo umount -f -l $MOUNT"
    sudo umount -f -l "$MOUNT" 2>/dev/null || true
  fi
else
  log "$MOUNT not mounted (skip unmount)."
fi

# 5) Remove mount dir only if no longer mounted
if ! is_mounted && [[ -d "$MOUNT" ]]; then
  log "Removing mount dir: sudo rm -rf --one-file-system $MOUNT"
  sudo rm -rf --one-file-system "$MOUNT" 2>/dev/null || true
fi

# 6) Reset failed state so systemd shuts up
sudo systemctl reset-failed "$UNIT" >/dev/null 2>&1 || true

# 7) Final blunt checks
if sudo pgrep -fa 'vaulthalla|fuse' >/dev/null 2>&1; then
  log "❌ Stragglers still alive:"
  sudo pgrep -fa 'vaulthalla|fuse' || true
  # exit 1
fi

if is_mounted; then
  log "❌ Mount still present at $MOUNT (kernel-wedged FUSE). Only a reboot clears that."
  # exit 1
fi

log "✅ Annihilated."
