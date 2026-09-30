#!/usr/bin/env bash
# Read-only health snapshot of the vh-storage production test lab.
# Every probe that could touch the FUSE mount is timeout-guarded: a wedged
# daemon puts stat/df/ls/mountpoint into uninterruptible sleep.
# Usage: inspect.sh [host]   (default: vh-storage)
set -uo pipefail
HOST="${1:-vh-storage}"

timeout 90 ssh -o BatchMode=yes -o ConnectTimeout=8 "$HOST" 'bash -s' <<'REMOTE'
set -uo pipefail
sec() { printf '\n=== %s\n' "$*"; }

sec "host"; hostname; uptime; uname -r
sec "package"; dpkg-query -W -f='${db:Status-Abbrev} ${Package} ${Version}\n' vaulthalla 2>/dev/null || echo "not installed"
sec "dpkg/apt activity"
ps -eo pid,etime,stat,cmd | grep -E '[a]pt(-get)? |[d]pkg |[v]aulthalla\.(postinst|prerm|postrm)|[u]nattended-upgr' | grep -v shutdown || echo "none"
sudo -n fuser /var/lib/dpkg/lock-frontend >/dev/null 2>&1 && echo "dpkg frontend lock HELD" || echo "dpkg lock free"
sec "units"
systemctl list-units 'vaulthalla*' --all --no-pager --plain --no-legend
for u in vaulthalla vaulthalla-web vaulthalla-cli postgresql@16-main nginx; do
  printf '%-22s ' "$u"; systemctl show "$u" -p ActiveState -p SubState -p NRestarts -p ActiveEnterTimestamp --value 2>/dev/null | paste -sd' '
done
sec "FUSE mount (mountinfo, no FUSE round-trip)"
grep ' /mnt/vaulthalla ' /proc/self/mountinfo || echo "not mounted"
sec "FUSE responsiveness (non-blocking: kernel fusectl counters, no FUSE round-trip)"
# Never stat/ls/df the mount here: a wedged daemon leaves those probes in
# uninterruptible sleep that neither timeout(1) nor SIGKILL can clear.
dev=$(awk '$5=="/mnt/vaulthalla"{split($3,a,":"); print a[2]}' /proc/self/mountinfo)
if [[ -n "$dev" && -r /sys/fs/fuse/connections ]]; then
  w1=$(sudo -n cat "/sys/fs/fuse/connections/$dev/waiting" 2>/dev/null); sleep 2
  w2=$(sudo -n cat "/sys/fs/fuse/connections/$dev/waiting" 2>/dev/null)
  echo "fuse conn $dev: waiting requests ${w1:-?} -> ${w2:-?} (2s apart)"
  [[ "${w2:-0}" -gt 0 && "${w1:-0}" -gt 0 ]] && echo "!! requests stuck in daemon: FUSE likely WEDGED"
fi
echo "processes in D-state on the mount:"
ps -eo pid,etime,stat,user,cmd | awk '$3 ~ /^D/' | grep -v ' \[' || echo "  none"
sec "daemon threads"
pid=$(systemctl show vaulthalla -p MainPID --value)
if [[ -n "$pid" && "$pid" != 0 ]]; then
  echo "MainPID=$pid exe=$(sudo -n readlink /proc/$pid/exe)"
  for t in /proc/$pid/task/*; do sudo -n cat "$t/wchan" 2>/dev/null; echo; done | sort | uniq -c | sort -rn | head -6
fi
sec "storage"; df -h -x fuse.vaulthalla-fuse -x tmpfs -x devtmpfs -x efivarfs 2>/dev/null
sec "TPM"; ls /dev/tpm* 2>/dev/null || echo "no hardware TPM"
sec "vaulthalla journal (warnings+, last 20)"
sudo -n journalctl -u vaulthalla -p warning --no-pager -n 20 2>/dev/null | cut -c1-240
sec "vaulthalla journal (last line)"
sudo -n journalctl -u vaulthalla --no-pager -n 1 -o short-iso 2>/dev/null | cut -c1-240
REMOTE
