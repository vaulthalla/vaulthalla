"""Behavioral tests: execute the real maintainer-script functions under dash with stub tools.

The function section of each script (everything before the final `case "$1" in`) is
sourced into a harness; systemctl/psql/runuser/pg_isready/id/chown/sleep are replaced by
stubs on PATH that log their argv/stdin. Nothing touches the host's PostgreSQL, systemd,
nginx or /mnt/vaulthalla.
Run by module: python3 -m unittest tools.release.tests.packaging.test_maintainer_script_behavior
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import textwrap
import unittest

SHELL = shutil.which("dash") or "/bin/sh"

FAKE_PSQL = r"""#!/bin/sh
# Fake psql: logs argv, answers the queries the maintainer scripts make, logs stdin SQL.
printf '%s\n' "$*" >> "$VH_TEST_LOG/psql.argv"
sql=""
while [ $# -gt 0 ]; do
    case "$1" in
        -c) sql="$2"; shift 2 ;;
        *) shift ;;
    esac
done
if [ -z "$sql" ]; then
    cat >> "$VH_TEST_LOG/psql.stdin"
    exit "${FAKE_PSQL_STDIN_RC:-0}"
fi
case "$sql" in
    *pg_roles*) [ "${FAKE_ROLE_EXISTS:-0}" = "1" ] && echo 1 ;;
    *pg_database*) [ "${FAKE_DB_EXISTS:-0}" = "1" ] && echo 1 ;;
    *to_regclass*) echo "${FAKE_USERS_TABLE:-f}" ;;
    *"FROM public.users"*) echo "${FAKE_USERS_ROWS:-f}" ;;
    *server_version_num*) echo 160004 ;;
    *) printf '%s\n' "$sql" >> "$VH_TEST_LOG/psql.exec" ;;
esac
exit 0
"""

FAKE_RUNUSER = """#!/bin/sh
# runuser -u postgres -- cmd...
shift 3
exec "$@"
"""

FAKE_SYSTEMCTL = r"""#!/bin/sh
# Fake systemctl: ActiveState walks a scripted sequence (one entry per poll).
printf '%s\n' "$*" >> "$VH_TEST_LOG/systemctl.argv"
case "$*" in
    *"-p ActiveState"*)
        n=$(cat "$VH_TEST_LOG/state.n" 2>/dev/null || echo 0)
        n=$((n + 1)); echo "$n" > "$VH_TEST_LOG/state.n"
        state=$(printf '%s\n' $FAKE_STATES | sed -n "${n}p")
        [ -n "$state" ] || state=$(printf '%s\n' $FAKE_STATES | tail -n 1)
        echo "$state"; echo "$state" > "$VH_TEST_LOG/state.last" ;;
    *"-p MainPID"*)
        if [ "$(cat "$VH_TEST_LOG/state.last" 2>/dev/null)" = "inactive" ]; then echo 0; else echo 4242; fi ;;
esac
exit 0
"""


def _write_exec(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


class _Harness:
    def __init__(self, script: str) -> None:
        self.repo = Path(__file__).resolve().parents[4]
        self.tmp = Path(tempfile.mkdtemp(prefix="vh-maint-"))
        self.bin = self.tmp / "bin"
        self.log = self.tmp / "log"
        self.state = self.tmp / "state"
        self.run_dir = self.tmp / "run"
        for d in (self.bin, self.log, self.state, self.run_dir):
            d.mkdir()
        text = (self.repo / "debian" / script).read_text(encoding="utf-8")
        functions = text[: text.rindex('\ncase "$1" in')]
        (self.tmp / "functions.sh").write_text(functions + "\n", encoding="utf-8")
        _write_exec(self.bin / "psql", FAKE_PSQL)
        _write_exec(self.bin / "pg_isready", "#!/bin/sh\nexit 0\n")
        _write_exec(self.bin / "runuser", FAKE_RUNUSER)
        _write_exec(self.bin / "systemctl", FAKE_SYSTEMCTL)
        _write_exec(self.bin / "id", "#!/bin/sh\nexit 0\n")
        _write_exec(self.bin / "chown", "#!/bin/sh\nexit 0\n")
        _write_exec(self.bin / "sleep", "#!/bin/sh\nexit 0\n")
        _write_exec(self.bin / "mountpoint", f"#!/bin/sh\necho called >> {self.log}/mountpoint.called\nexit 0\n")
        self.config = self.tmp / "config.yaml"
        self.config.write_text("database:\n  host: localhost\n  port: 5432\n", encoding="utf-8")

    def cleanup(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run(self, body: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        preamble = textwrap.dedent(
            f"""
            . "{self.tmp}/functions.sh"
            STATE_DIR="{self.state}"
            RUNTIME_DIR="{self.run_dir}"
            PENDING_DB_PASS_FILE="{self.run_dir}/db_password"
            SEALED_DB_SECRET_DIR="{self.state}/.sealed_psql.blob"
            DB_BOOTSTRAP_OPTOUT_MARKER="{self.state}/db_bootstrap_disabled"
            CONFIG_FILE="{self.config}"
            create_dir() {{ mkdir -p "$1"; }}
            """
        )
        full_env = {
            "PATH": f"{self.bin}:{os.environ.get('PATH', '/usr/bin:/bin')}",
            "VH_TEST_LOG": str(self.log),
            "DEBIAN_FRONTEND": "noninteractive",
            "HOME": str(self.tmp),
        }
        full_env.update(env or {})
        return subprocess.run(
            [SHELL, "-c", preamble + textwrap.dedent(body)],
            capture_output=True,
            text=True,
            env=full_env,
            stdin=subprocess.DEVNULL,
            timeout=60,
        )

    def read_log(self, name: str) -> str:
        path = self.log / name
        return path.read_text(encoding="utf-8") if path.exists() else ""


class PostinstDbBootstrapBehaviorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.h = _Harness("postinst")
        self.addCleanup(self.h.cleanup)

    def _bootstrap(self, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        return self.h.run('bootstrap_db_if_safe\necho "STATUS=$DB_BOOTSTRAP_STATUS"\n', env)

    def _seed(self) -> Path:
        return self.h.run_dir / "db_password"

    def test_fresh_bootstrap_seeds_before_creating_role_and_keeps_password_out_of_argv(self) -> None:
        result = self._bootstrap()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("STATUS=done (created local DB role/database", result.stdout)
        password = self._seed().read_text(encoding="utf-8").strip()
        self.assertRegex(password, r"^[0-9a-f]{64}$")
        self.assertEqual(stat.S_IMODE(self._seed().stat().st_mode), 0o600)
        self.assertIn(f"CREATE ROLE vaulthalla LOGIN PASSWORD '{password}';", self.h.read_log("psql.stdin"))
        self.assertNotIn(password, self.h.read_log("psql.argv"))
        self.assertIn("CREATE DATABASE vaulthalla OWNER vaulthalla", self.h.read_log("psql.exec"))

    def test_orphan_with_data_aborts_noninteractively_with_resolution_commands(self) -> None:
        env = {"FAKE_ROLE_EXISTS": "1", "FAKE_DB_EXISTS": "1", "FAKE_USERS_TABLE": "t", "FAKE_USERS_ROWS": "t"}
        result = self._bootstrap(env)
        self.assertEqual(result.returncode, 1)
        self.assertIn("sudo env VH_EXISTING_DB_ACTION=adopt dpkg --configure -a", result.stderr)
        self.assertIn("sudo env VH_EXISTING_DB_ACTION=overwrite dpkg --configure -a", result.stderr)
        self.assertEqual(self.h.read_log("psql.stdin"), "")
        self.assertFalse(self._seed().exists())

    def test_orphan_adopt_rotates_password_via_stdin_and_warns_about_keys(self) -> None:
        env = {
            "FAKE_ROLE_EXISTS": "1", "FAKE_DB_EXISTS": "1", "FAKE_USERS_TABLE": "t", "FAKE_USERS_ROWS": "t",
            "VH_EXISTING_DB_ACTION": "adopt",
        }
        result = self._bootstrap(env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("STATUS=done (adopted existing DB", result.stdout)
        self.assertIn(".sealed_*.blob", result.stderr)
        password = self._seed().read_text(encoding="utf-8").strip()
        self.assertIn(f"ALTER ROLE vaulthalla WITH LOGIN PASSWORD '{password}';", self.h.read_log("psql.stdin"))
        self.assertNotIn(password, self.h.read_log("psql.argv"))
        self.assertNotIn("DROP DATABASE", self.h.read_log("psql.exec"))

    def test_orphan_overwrite_drops_with_force_then_bootstraps(self) -> None:
        env = {
            "FAKE_ROLE_EXISTS": "1", "FAKE_DB_EXISTS": "1", "FAKE_USERS_TABLE": "t", "FAKE_USERS_ROWS": "t",
            "VH_EXISTING_DB_ACTION": "overwrite",
        }
        result = self._bootstrap(env)
        self.assertEqual(result.returncode, 0, result.stderr)
        executed = self.h.read_log("psql.exec")
        self.assertIn("DROP DATABASE IF EXISTS vaulthalla WITH (FORCE)", executed)
        self.assertIn("DROP ROLE IF EXISTS vaulthalla", executed)
        self.assertLess(executed.index("DROP DATABASE"), executed.index("CREATE DATABASE"))
        self.assertIn("CREATE ROLE vaulthalla LOGIN PASSWORD", self.h.read_log("psql.stdin"))
        self.assertIn("STATUS=done (existing DB overwritten", result.stdout)

    def test_invalid_action_aborts(self) -> None:
        env = {
            "FAKE_ROLE_EXISTS": "1", "FAKE_DB_EXISTS": "1", "FAKE_USERS_TABLE": "t", "FAKE_USERS_ROWS": "t",
            "VH_EXISTING_DB_ACTION": "yolo",
        }
        result = self._bootstrap(env)
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid VH_EXISTING_DB_ACTION", result.stderr)

    def test_empty_orphan_from_interrupted_install_is_adopted_automatically(self) -> None:
        result = self._bootstrap({"FAKE_ROLE_EXISTS": "1", "FAKE_DB_EXISTS": "1", "FAKE_USERS_TABLE": "f"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("STATUS=done (adopted empty DB", result.stdout)
        self.assertIn("ALTER ROLE vaulthalla WITH LOGIN PASSWORD", self.h.read_log("psql.stdin"))

    def test_pending_seed_is_resumed_not_regenerated(self) -> None:
        self._seed().write_text("feedface\n", encoding="utf-8")
        result = self._bootstrap({"FAKE_ROLE_EXISTS": "1"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("STATUS=done (resumed from pending runtime password handoff", result.stdout)
        self.assertIn("ALTER ROLE vaulthalla WITH LOGIN PASSWORD 'feedface';", self.h.read_log("psql.stdin"))
        self.assertEqual(self._seed().read_text(encoding="utf-8").strip(), "feedface")

    def test_sealed_secret_means_no_db_changes(self) -> None:
        sealed = self.h.state / ".sealed_psql.blob"
        sealed.mkdir()
        (sealed / "psql.priv").write_bytes(b"x")
        result = self._bootstrap({"FAKE_ROLE_EXISTS": "1", "FAKE_DB_EXISTS": "1"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("STATUS=reused (sealed DB credential present", result.stdout)
        self.assertEqual(self.h.read_log("psql.argv"), "")

    def test_remote_database_host_is_never_bootstrapped(self) -> None:
        self.h.config.write_text("database:\n  host: db.example.net   # remote\n", encoding="utf-8")
        result = self._bootstrap()
        self.assertIn("STATUS=skipped (config.yaml database.host is remote: db.example.net)", result.stdout)
        self.assertEqual(self.h.read_log("psql.argv"), "")
        self.assertFalse(self._seed().exists())

    def test_skip_db_bootstrap_opt_out_persists(self) -> None:
        first = self._bootstrap({"VH_SKIP_DB_BOOTSTRAP": "1"})
        self.assertIn("persisted at", first.stdout)
        second = self._bootstrap()
        self.assertIn("STATUS=skipped (persisted opt-out", second.stdout)
        self.assertEqual(self.h.read_log("psql.argv"), "")

    def test_role_failure_rolls_back_seed(self) -> None:
        result = self._bootstrap({"FAKE_PSQL_STDIN_RC": "1"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("STATUS=failed (could not create PostgreSQL role", result.stdout)
        self.assertFalse(self._seed().exists())


class MountDetectionBehaviorTests(unittest.TestCase):
    def test_is_mountpoint_uses_mountinfo_and_never_calls_mountpoint(self) -> None:
        for script in ("postinst", "postrm"):
            h = _Harness(script)
            self.addCleanup(h.cleanup)
            result = h.run(
                f"""
                is_mountpoint /proc && echo proc=yes
                is_mountpoint "{h.tmp}" || echo tmp=no
                is_mountpoint /mnt/vaulthalla-definitely-not-mounted || echo absent=no
                """
            )
            self.assertIn("proc=yes", result.stdout, script)
            self.assertIn("tmp=no", result.stdout, script)
            self.assertIn("absent=no", result.stdout, script)
            self.assertEqual(h.read_log("mountpoint.called"), "", script)


class PostrmPurgeTreeBehaviorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.h = _Harness("postrm")
        self.addCleanup(self.h.cleanup)
        self.target = self.h.tmp / "data"
        (self.target / "vaults" / "a").mkdir(parents=True)
        (self.target / "vaults" / "a" / "blob").write_bytes(b"x")
        (self.target / ".sealed_psql.blob").mkdir()

    def test_plain_directory_is_removed(self) -> None:
        result = self.h.run(f'purge_tree "{self.target}"\necho rc=$?\n')
        self.assertIn("rc=0", result.stdout)
        self.assertFalse(self.target.exists())

    def test_mount_point_keeps_directory_and_removes_contents(self) -> None:
        result = self.h.run(
            f"""
            is_mountpoint() {{ [ "$1" = "{self.target}" ]; }}
            purge_tree "{self.target}"
            echo rc=$?
            """
        )
        self.assertIn("rc=0", result.stdout)
        self.assertIn("is a mount point: removed its contents", result.stdout)
        self.assertTrue(self.target.is_dir())
        self.assertEqual(list(self.target.iterdir()), [])


class PostrmConfigPurgeBehaviorTests(unittest.TestCase):
    def test_purge_removes_package_config_but_keeps_operator_files(self) -> None:
        h = _Harness("postrm")
        self.addCleanup(h.cleanup)
        etc = h.tmp / "etc-vaulthalla"
        (etc / "testing").mkdir(parents=True)
        (etc / "certbot").mkdir()
        (etc / "testing" / "providers.env").write_text("AWS_ACCESS_KEY_ID=placeholder\n", encoding="utf-8")
        (etc / "certbot" / "cloudflare.ini").write_text("dns_cloudflare_api_token=placeholder\n", encoding="utf-8")
        for name in ("config.yaml", "config_template.yaml.in", "config.yaml.dpkg-old"):
            (etc / name).write_text("x\n", encoding="utf-8")
        result = h.run(f'CONFIG_DIR="{etc}"\nCERTBOT_CREDENTIALS_DIR="{etc}/certbot"\npurge_config_dir\necho rc=$?\n')
        self.assertIn("rc=0", result.stdout)
        self.assertFalse((etc / "config.yaml").exists())
        self.assertFalse((etc / "config_template.yaml.in").exists())
        self.assertFalse((etc / "config.yaml.dpkg-old").exists())
        self.assertTrue((etc / "testing" / "providers.env").is_file())
        self.assertTrue((etc / "certbot" / "cloudflare.ini").is_file())
        self.assertIn("Preserved operator-managed", result.stdout)

    def test_purge_removes_empty_config_dir(self) -> None:
        h = _Harness("postrm")
        self.addCleanup(h.cleanup)
        etc = h.tmp / "etc-vaulthalla"
        etc.mkdir()
        (etc / "config.yaml").write_text("x\n", encoding="utf-8")
        h.run(f'CONFIG_DIR="{etc}"\nCERTBOT_CREDENTIALS_DIR="{etc}/certbot"\npurge_config_dir\n')
        self.assertFalse(etc.exists())


class PrermStopBehaviorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.h = _Harness("prerm")
        self.addCleanup(self.h.cleanup)

    def _stop(self, states: str) -> subprocess.CompletedProcess[str]:
        return self.h.run(
            """
            systemd_running() { return 0; }
            stop_service_bounded vaulthalla.service
            echo rc=$?
            """,
            {"FAKE_STATES": states},
        )

    def test_waits_through_deactivating_without_killing(self) -> None:
        result = self._stop("active deactivating deactivating deactivating inactive")
        self.assertIn("rc=0", result.stdout)
        calls = self.h.read_log("systemctl.argv")
        self.assertIn("stop --no-block vaulthalla.service", calls)
        self.assertNotIn("kill", calls)
        self.assertGreaterEqual(calls.count("-p ActiveState"), 5)

    def test_escalates_to_cgroup_sigkill_when_stop_never_finishes(self) -> None:
        result = self._stop("deactivating")
        self.assertIn("rc=0", result.stdout)
        calls = self.h.read_log("systemctl.argv")
        self.assertIn("kill -s KILL vaulthalla.service", calls)
        self.assertIn("reset-failed vaulthalla.service", calls)


if __name__ == "__main__":
    unittest.main()
