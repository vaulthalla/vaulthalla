<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Restarts in about a second

`systemctl restart vaulthalla`, package upgrades and `make dev` used to take 5 seconds on a freshly started daemon
and, once it had been running for a while, 30 seconds ending in a forced kill. The daemon now stops in tens of
milliseconds and a full restart takes about a second.

- **No more 30-second hang on stop.** The thread pools could lend a worker to another pool; stopping them then
  waited forever on a worker parked on the wrong queue, until systemd killed the process. Workers are no longer lent,
  and every pool stops within a deadline.
- **Stopping no longer waits out timers.** Services, the watchdog and the sync scheduler wake the moment a stop is
  requested instead of finishing their current sleep, and all services stop in parallel (FUSE last).
- **A held mount can't block shutdown.** If something still has the mount open (a shell sitting in
  `/mnt/vaulthalla`, an open file), the daemon aborts its FUSE connection after one second instead of hanging; the
  journal names the connection it aborted.
- **A hard limit.** From the first SIGTERM the daemon exits within 10 seconds whatever it is doing, including a
  startup stuck on an unreachable database; a second SIGTERM exits at once.
- **Faster startup.** The daemon no longer computes four deliberately slow password hashes on every start.
- The Runtime health page no longer shows the "Borrowed" worker column (workers are not lent anymore).

