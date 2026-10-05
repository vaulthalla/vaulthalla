"""Static safety contract for the Debian maintainer scripts.

Pins the invariants behind the 2026-09-30 lab incidents:
- an `apt upgrade` hung forever in postinst `mountpoint -q /mnt/vaulthalla` on a wedged FUSE daemon
- purge failed permanently because /var/lib/vaulthalla was a mount point (EBUSY under `set -e`)
- prerm returned while the daemon was still `deactivating`, so postrm's userdel raced it
Run by module: python3 -m unittest tools.contracts.test_maintainer_script_safety
"""

from __future__ import annotations

from pathlib import Path
import re
import unittest

SCRIPTS = ("preinst", "postinst", "prerm", "postrm")


def _strip_quoted(line: str) -> str:
    # Drop quoted string contents so log messages that mention a command don't count.
    line = re.sub(r'"(?:[^"\\]|\\.)*"', '""', line)
    return re.sub(r"'[^']*'", "''", line)


class MaintainerScriptSafetyTests(unittest.TestCase):
    def _repo_root(self) -> Path:
        return Path(__file__).resolve().parents[2]

    def _script(self, name: str) -> str:
        return (self._repo_root() / "debian" / name).read_text(encoding="utf-8")

    def _code_lines(self, name: str) -> list[tuple[int, str]]:
        lines = []
        for number, raw in enumerate(self._script(name).splitlines(), start=1):
            stripped = raw.strip()
            if not stripped or stripped.startswith("#"):
                continue
            lines.append((number, _strip_quoted(raw)))
        return lines

    def _function_body(self, script: str, name: str) -> str:
        start = script.index(f"{name}() {{")
        end = script.index("\n}\n", start)
        return script[start:end]

    def test_upgrade_path_terminal_output_goes_through_the_log_helpers(self) -> None:
        # Routine configure/remove output is log-only (detail); only the helpers and the end-of-run
        # status line may write "[vaulthalla] ..." to the terminal directly.
        allowed = {
            "postinst": ('echo "[${PKG}] $*"', 'echo "[${PKG}] $*" >&2'),
            "prerm": ('echo "[${PKG}] $*"', 'echo "[${PKG}] WARNING: $*" >&2',
                      'echo "[${PKG}] Stopped and disabled Vaulthalla services. Log: ${PACKAGE_LOG}"'),
        }
        for name, ok in allowed.items():
            for number, raw in enumerate(self._script(name).splitlines(), start=1):
                stripped = raw.strip()
                if stripped.startswith('echo "[${PKG}]') and stripped not in ok and not stripped.startswith("1|true"):
                    self.fail(f"{name}:{number}: raw terminal output bypasses the package log: {stripped}")

    def test_package_log_is_root_owned_and_symlink_safe(self) -> None:
        for name in ("postinst", "prerm"):
            script = self._script(name)
            self.assertIn('PACKAGE_LOG="/var/log/vaulthalla-package.log"', script, name)
            self.assertIn('[ -L "$PACKAGE_LOG" ]', script, name)
        self.assertIn('rm -f "$package_log"', self._script("postrm"))

    def test_scripts_are_posix_sh_with_errexit(self) -> None:
        for name in SCRIPTS:
            text = self._script(name)
            self.assertTrue(text.startswith("#!/bin/sh\nset -e\n"), name)
            self.assertIn("#DEBHELPER#", text, name)

    def test_debhelper_token_only_appears_as_a_standalone_line(self) -> None:
        # dh_installdeb substitutes the token anywhere, including inside comments,
        # which would splice generated code into a comment line.
        for name in SCRIPTS:
            for number, raw in enumerate(self._script(name).splitlines(), start=1):
                if "#DEBHELPER#" in raw:
                    self.assertEqual(raw.strip(), "#DEBHELPER#", f"{name}:{number}")

    def test_no_fuse_touching_mount_probes(self) -> None:
        # Any syscall on a live FUSE mount blocks forever if the daemon is wedged.
        forbidden = re.compile(r"\b(mountpoint|stat|ls|du|df|findmnt|find|test)\b[^|;&]*(/mnt/vaulthalla|MOUNT_ROOT)")
        for name in SCRIPTS:
            for number, line in self._code_lines(name):
                self.assertNotRegex(line, r"(^|[\s;&|(])mountpoint\s", f"{name}:{number} uses mountpoint(1)")
                self.assertIsNone(forbidden.search(line), f"{name}:{number} probes the FUSE mount: {line.strip()}")

    def test_mountpoint_detection_parses_kernel_mount_table(self) -> None:
        for name in ("postinst", "postrm"):
            body = self._function_body(self._script(name), "is_mountpoint")
            self.assertIn("/proc/self/mountinfo", body, name)
            self.assertIn('ENVIRON["VH_MOUNTINFO_TARGET"]', body, name)
            self.assertNotIn("mountpoint -q", body, name)

    def test_mount_root_checks_mountinfo_before_any_stat(self) -> None:
        body = self._function_body(self._script("postinst"), "ensure_mount_root")
        self.assertLess(body.index('if is_mountpoint "$path"; then'), body.index('if [ -d "$path" ]; then'))

    def test_every_systemctl_call_is_bounded(self) -> None:
        call = re.compile(r"(^|[\s;&|(!`$])systemctl\s")
        for name in SCRIPTS:
            for number, line in self._code_lines(name):
                if not call.search(line) or "has_command systemctl" in line:
                    continue
                self.assertIn("run_bounded", line, f"{name}:{number} runs systemctl without a timeout")

    def test_every_psql_and_nginx_and_udev_call_is_bounded(self) -> None:
        for name in SCRIPTS:
            text = self._script(name)
            for number, line in self._code_lines(name):
                if re.search(r"(^|[\s;&|(])(psql|pg_isready)\s", line) and "has_command" not in line:
                    self.assertIn("run_as_postgres", line, f"{name}:{number} runs psql outside run_as_postgres")
                if re.search(r"(^|[\s;&|(])nginx -t", line):
                    self.assertIn("run_bounded", line, f"{name}:{number} runs nginx -t without a timeout")
                if re.search(r"(^|[\s;&|(])udevadm\s", line):
                    self.assertIn("run_bounded", line, f"{name}:{number} runs udevadm without a timeout")
            if "run_as_postgres()" in text:
                body = self._function_body(text, "run_as_postgres")
                self.assertEqual(body.count("run_bounded"), 2, f"{name}: both runuser and sudo paths must be bounded")
                self.assertIn("PGCONNECT_TIMEOUT", body, name)

    def test_no_substring_process_kills(self) -> None:
        for name in SCRIPTS:
            for number, line in self._code_lines(name):
                self.assertNotRegex(line, r"\b(pkill|killall)\b|pgrep\s+-f|fuser\s+-k", f"{name}:{number}")
                # kill(1) in command position; `systemctl kill` (cgroup-scoped) is the allowed form.
                if re.search(r"(^\s*|[;&|(]\s*|\b(then|do|else)\s+)kill\s", line):
                    self.fail(f"{name}:{number} uses kill(1); use 'systemctl kill' on the unit cgroup")

    def test_postinst_restarts_are_bounded_with_cgroup_kill_fallback(self) -> None:
        body = self._function_body(self._script("postinst"), "transition_unit_bounded")
        self.assertIn('run_bounded "$SERVICE_TRANSITION_TIMEOUT_SECONDS" systemctl --system "$verb" "$unit"', body)
        self.assertIn('kill -s KILL "$unit"', body)
        self.assertIn("reset-failed", body)
        self.assertIn("return 0", body.splitlines()[-1] + body[-40:])

    def test_prerm_waits_for_terminal_state_and_no_main_pid(self) -> None:
        prerm = self._script("prerm")
        body = self._function_body(prerm, "unit_fully_stopped")
        self.assertIn("ActiveState", body)
        self.assertIn("MainPID", body)
        self.assertIn("inactive|failed)", body)
        stop_body = self._function_body(prerm, "stop_service_bounded")
        self.assertNotIn("is-active", stop_body)
        self.assertLess(stop_body.index("wait_for_unit_stopped"), stop_body.index('kill -s KILL "$unit"'))
        wait_seconds = int(re.search(r"SYSTEMCTL_STOP_WAIT_SECONDS=(\d+)", prerm).group(1))
        unit = (self._repo_root() / "deploy" / "systemd" / "vaulthalla.service.in").read_text(encoding="utf-8")
        stop_timeout = int(re.search(r"TimeoutStopSec=(\d+)", unit).group(1))
        self.assertGreater(wait_seconds, stop_timeout, "prerm must outwait the unit's own TimeoutStopSec")

    def test_purge_tolerates_mount_points_and_never_fails(self) -> None:
        postrm = self._script("postrm")
        self.assertNotRegex(postrm, r"rm -rf /var/lib/vaulthalla")
        self.assertNotRegex(postrm, r'rm -rf "\$STATE_DIR"')
        self.assertIn("purge_package_state || true", postrm)
        body = self._function_body(postrm, "purge_tree")
        self.assertIn("-xdev", body)
        self.assertIn('if is_mountpoint "$dir"; then', body)
        self.assertIn('chown root:root "$dir"', body)
        self.assertIn('chmod 0755 "$dir"', body)
        purge = self._function_body(postrm, "purge_package_state")
        self.assertIn('purge_tree "$STATE_DIR"', purge)
        # The account goes before the state dir is re-owned to root.
        self.assertLess(purge.index("remove_service_account"), purge.index('purge_tree "$STATE_DIR"'))

    def test_purge_cleans_runtime_leftovers(self) -> None:
        postrm = self._script("postrm")
        for fragment in (
            'cache_link="${WEB_ROOT}/.next/cache"',
            'purge_tree "$WEB_CACHE_DIR"',
            "vaulthalla-nginx-reload.sh",
            "remove_renewal_deploy_hook",
            'rm -f "${CONFIG_DIR}/${f}"',
            "restore_nginx_default_site_if_disabled",
        ):
            self.assertIn(fragment, postrm)

    def test_etc_vaulthalla_is_never_removed_recursively(self) -> None:
        # /etc/vaulthalla/testing/providers.env (operator-managed test credentials) and
        # /etc/vaulthalla/certbot/ must survive upgrades and purge.
        recursive = re.compile(r"\brm\s+-[a-zA-Z]*[rR]|-delete\b|\bpurge_tree\b")
        etc_target = re.compile(r"/etc/vaulthalla|\$\{?CONFIG_DIR\}?|CERTBOT_CREDENTIALS_DIR")
        for name in SCRIPTS:
            for number, line in self._code_lines(name):
                if recursive.search(line):
                    self.assertIsNone(etc_target.search(line), f"{name}:{number} recursively removes under /etc/vaulthalla")
            self.assertNotIn("/etc/vaulthalla/testing", self._script(name).replace("${CONFIG_DIR}/testing", ""), name)
        purge = self._function_body(self._script("postrm"), "purge_config_dir")
        self.assertIn('rmdir "$CONFIG_DIR"', purge)
        self.assertIn('"${CONFIG_DIR}/testing"', purge)

    def test_purge_message_works_without_vh_installed(self) -> None:
        postrm = self._script("postrm")
        self.assertNotIn("sudo vh teardown db", postrm)
        self.assertIn("sudo -u postgres psql -c 'DROP DATABASE IF EXISTS ${DB_NAME} WITH (FORCE);'", postrm)
        self.assertIn("sudo -u postgres psql -c 'DROP ROLE IF EXISTS ${DB_USER};'", postrm)

    def test_drop_database_is_never_batched_with_other_statements(self) -> None:
        for name in ("postinst", "postrm"):
            text = self._script(name)
            for block in re.findall(r"<<EOF\n(.*?)\nEOF", text, flags=re.S):
                self.assertNotIn("DROP DATABASE", block, f"{name}: DROP DATABASE inside a heredoc batch")


if __name__ == "__main__":
    unittest.main()
