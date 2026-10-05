# Quiet package upgrades

`apt upgrade` no longer prints a fifteen-line status report for Vaulthalla. A clean upgrade now prints one line:

```
[vaulthalla] Upgraded 1.9.1-1 -> 1.9.2-1: services restarted, daemon healthy. Log: /var/log/vaulthalla-package.log
```

A second `Action:` line appears only when something is waiting on you (for example, the generated web `admin` password file still exists). Warnings still print, one line each. A hard failure, such as the daemon not running after the restart, a missing CLI socket, no TPM backend or a failed database bootstrap, prints an error and the full summary. Fresh installs and reinstalls still print the full summary with the database, TPM, nginx and web console next steps.

Every step, the full summary and systemctl output are now kept in `/var/log/vaulthalla-package.log` (root-readable, rotated at 1 MiB, removed on purge). To see everything on the terminal, run `sudo env VH_PACKAGE_VERBOSE=1 dpkg-reconfigure vaulthalla`. `apt remove` now prints a single line when the services stop cleanly.
