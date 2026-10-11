<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
- Bounded, fast daemon shutdown (restart ~30 s/5 s -> ~1 s):
  - ThreadPool: drop worker lending between pools (a lent worker kept serving its own queue, and stopping the
    borrower joined it forever); two-phase stop (requestStop all, join against one deadline, detach stragglers that
    hold only the shared state); waitIdle(). ThreadPoolManager loses its rebalance monitor.
  - AsyncService: lazySleep waits on a condition variable; requestStop()/join() split; Manager::stopAll stops every
    service but FUSE in parallel, then FUSE; the watchdog waits on a condition variable.
  - FUSE: unmount moved to onStop(); after a 1 s grace the fusectl connection (from /proc/self/mountinfo) is aborted
    if the mount is still referenced; requests drain before fuse_session_destroy; libfuse signal handlers dropped.
  - main: async-signal-safe SIGTERM/SIGINT handler (atomic + eventfd), watcher thread with a 10 s exit deadline from
    the first signal, second signal exits at once.
  - SyncController lost-wakeup fix; RetentionService uses lazySleep; HTTP/S3 session drains 10 s -> 3 s.
- Startup: reconcileSystemPrincipals hashes passwords only for missing principals; the retired-default-password
  check re-verifies only when admin's hash changed (marker in the backing directory).
- Remove the borrowed-worker metric from thread pool stats, the dashboard and the Runtime page; the
  stats_threadpool_sample borrowed_workers_* columns are no longer written (kept, defaulting to 0, for rollback).

