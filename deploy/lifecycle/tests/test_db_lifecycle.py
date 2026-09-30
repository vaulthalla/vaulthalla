import argparse
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from deploy.lifecycle import main

PREFIX = ["runuser", "-u", "postgres", "--", "env", "PGCONNECT_TIMEOUT=10", "psql", "-X", "-v", "ON_ERROR_STOP=1"]


def _done(stdout: str = "", returncode: int = 0) -> subprocess.CompletedProcess[str]:
    return subprocess.CompletedProcess(args=[], returncode=returncode, stdout=stdout, stderr="")


class FakePostgres:
    """Records every run_capture call and answers the lifecycle's catalog queries."""

    def __init__(self, role: bool = False, db: bool = False, users_table: bool = False, users_rows: bool = False):
        self.role = role
        self.db = db
        self.users_table = users_table
        self.users_rows = users_rows
        self.calls: list[tuple[list[str], str | None]] = []

    def __call__(self, args, input_text=None, timeout=None):
        self.calls.append((list(args), input_text))
        sql = args[-1] if "-tAc" in args else ""
        if "pg_roles" in sql:
            return _done("1\n" if self.role else "")
        if "pg_database" in sql:
            return _done("1\n" if self.db else "")
        if "to_regclass" in sql:
            return _done("t\n" if self.users_table else "f\n")
        if "FROM public.users" in sql:
            return _done("t\n" if self.users_rows else "f\n")
        if "server_version_num" in sql:
            return _done("160004\n")
        return _done()

    def executed_sql(self) -> list[str]:
        return [args[-1] for args, _ in self.calls if "-tAc" in args]

    def stdin_sql(self) -> str:
        return "".join(text for _, text in self.calls if text)

    def argv_text(self) -> str:
        return "\n".join(" ".join(args) for args, _ in self.calls)


class DbLifecycleTestBase(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="vh-lifecycle-"))
        schemas = self.tmp / "psql"
        schemas.mkdir()
        (schemas / "000_schema.sql").write_text("SELECT 1;\n", encoding="utf-8")
        self.seed = self.tmp / "run" / "db_password"
        self.sealed = self.tmp / "state" / ".sealed_psql.blob"
        self.optout = self.tmp / "state" / "db_bootstrap_disabled"
        patches = [
            mock.patch.object(main, "DEFAULT_SCHEMA_DIR", schemas),
            mock.patch.object(main, "PENDING_DB_PASSWORD_FILE", self.seed),
            mock.patch.object(main, "SEALED_DB_SECRET_DIR", self.sealed),
            mock.patch.object(main, "DB_BOOTSTRAP_OPTOUT_MARKER", self.optout),
            mock.patch.object(main, "command_exists", return_value=True),
            mock.patch.object(main, "choose_postgres_prefix", return_value=list(PREFIX)),
            mock.patch.object(main.os, "chown"),
            mock.patch.object(main.pwd, "getpwnam", return_value=mock.Mock(pw_uid=os.getuid(), pw_gid=os.getgid())),
            mock.patch.object(main, "restart_or_start_service", return_value="started"),
        ]
        for p in patches:
            p.start()
            self.addCleanup(p.stop)

    def run_setup(self, fake: FakePostgres, *flags: str, healthy: bool = True) -> int:
        args = main.build_parser().parse_args(["setup", "db", *flags])
        detail = "active/running (stable for 5s)" if healthy else "activating/auto-restart; restarts=3 (was 0)"
        with mock.patch.object(main, "run_capture", side_effect=fake), \
                mock.patch.object(main, "wait_for_service_healthy", return_value=(healthy, detail)), \
                mock.patch("builtins.print"), mock.patch.object(main, "eprint"):
            return main.setup_db(args)


class SetupDbTests(DbLifecycleTestBase):
    def test_fresh_setup_creates_role_via_stdin_and_seeds_first(self) -> None:
        fake = FakePostgres()
        self.assertEqual(self.run_setup(fake), 0)
        password = self.seed.read_text(encoding="utf-8").strip()
        self.assertEqual(oct(self.seed.stat().st_mode & 0o777), "0o600")
        self.assertIn(f"CREATE ROLE vaulthalla LOGIN PASSWORD '{password}';", fake.stdin_sql())
        self.assertNotIn(password, fake.argv_text())
        self.assertIn("CREATE DATABASE vaulthalla OWNER vaulthalla;", fake.executed_sql())

    def test_orphan_with_data_is_refused_without_flag(self) -> None:
        fake = FakePostgres(role=True, db=True, users_table=True, users_rows=True)
        with self.assertRaisesRegex(main.LifecycleError, "--adopt.*--overwrite"):
            self.run_setup(fake)
        self.assertFalse(self.seed.exists())
        self.assertEqual(fake.stdin_sql(), "")

    def test_adopt_rotates_existing_role_password(self) -> None:
        fake = FakePostgres(role=True, db=True, users_table=True, users_rows=True)
        self.assertEqual(self.run_setup(fake, "--adopt"), 0)
        password = self.seed.read_text(encoding="utf-8").strip()
        self.assertIn(f"ALTER ROLE vaulthalla WITH LOGIN PASSWORD '{password}';", fake.stdin_sql())
        self.assertNotIn(password, fake.argv_text())

    def test_overwrite_drops_database_with_force_in_its_own_statement(self) -> None:
        fake = FakePostgres(role=True, db=True, users_table=True, users_rows=True)
        self.assertEqual(self.run_setup(fake, "--overwrite"), 0)
        executed = fake.executed_sql()
        self.assertIn("DROP DATABASE IF EXISTS vaulthalla WITH (FORCE);", executed)
        self.assertIn("DROP ROLE IF EXISTS vaulthalla;", executed)
        self.assertIn("CREATE ROLE vaulthalla LOGIN PASSWORD", fake.stdin_sql())
        for statement in executed:
            self.assertLessEqual(statement.count(";"), 1, statement)

    def test_adopt_and_overwrite_are_mutually_exclusive(self) -> None:
        with self.assertRaises(SystemExit), mock.patch("sys.stderr"):
            main.build_parser().parse_args(["setup", "db", "--adopt", "--overwrite"])

    def test_empty_orphan_is_adopted_automatically(self) -> None:
        fake = FakePostgres(role=True, db=True, users_table=False)
        self.assertEqual(self.run_setup(fake), 0)
        self.assertIn("ALTER ROLE vaulthalla WITH LOGIN PASSWORD", fake.stdin_sql())

    def test_sealed_credential_leaves_password_unchanged(self) -> None:
        self.sealed.mkdir(parents=True)
        (self.sealed / "psql.priv").write_bytes(b"x")
        fake = FakePostgres(role=True, db=True, users_table=True, users_rows=True)
        self.assertEqual(self.run_setup(fake), 0)
        self.assertEqual(fake.stdin_sql(), "")
        self.assertFalse(self.seed.exists())

    def test_unhealthy_service_is_reported_as_failure(self) -> None:
        fake = FakePostgres()
        with self.assertRaisesRegex(main.LifecycleError, "not running correctly.*journalctl"):
            self.run_setup(fake, healthy=False)

    def test_setup_db_removes_package_opt_out_marker(self) -> None:
        self.optout.parent.mkdir(parents=True)
        self.optout.write_text("VH_SKIP_DB_BOOTSTRAP=1\n", encoding="utf-8")
        self.assertEqual(self.run_setup(FakePostgres()), 0)
        self.assertFalse(self.optout.exists())

    def test_role_failure_rolls_back_seed(self) -> None:
        fake = FakePostgres()

        def failing(args, input_text=None, timeout=None):
            if input_text:
                fake.calls.append((list(args), input_text))
                return _done(returncode=1)
            return fake(args, input_text, timeout)

        args = main.build_parser().parse_args(["setup", "db"])
        with mock.patch.object(main, "run_capture", side_effect=failing), mock.patch("builtins.print"):
            with self.assertRaisesRegex(main.LifecycleError, "failed to create PostgreSQL role") as ctx:
                main.setup_db(args)
        self.assertNotIn("PASSWORD", str(ctx.exception))
        self.assertFalse(self.seed.exists())


class TeardownDbTests(DbLifecycleTestBase):
    def test_teardown_stops_service_and_runs_one_statement_per_call(self) -> None:
        fake = FakePostgres(role=True, db=True)
        self.seed.parent.mkdir(parents=True)
        self.seed.write_text("x\n", encoding="utf-8")
        with mock.patch.object(main, "run_capture", side_effect=fake), mock.patch("builtins.print"):
            self.assertEqual(main.teardown_db(argparse.Namespace()), 0)
        calls = [args for args, _ in fake.calls]
        self.assertIn(["systemctl", "stop", main.SERVICE_UNIT], calls)
        executed = fake.executed_sql()
        self.assertIn("DROP DATABASE IF EXISTS vaulthalla WITH (FORCE);", executed)
        self.assertIn("DROP ROLE IF EXISTS vaulthalla;", executed)
        for statement in executed:
            self.assertLessEqual(statement.count(";"), 1, statement)
        self.assertFalse(self.seed.exists())

    def test_teardown_on_old_postgres_terminates_then_drops_separately(self) -> None:
        fake = FakePostgres(role=True, db=True)
        original = fake.__call__

        def old_pg(args, input_text=None, timeout=None):
            if "-tAc" in args and "server_version_num" in args[-1]:
                fake.calls.append((list(args), input_text))
                return _done("120015\n")
            return original(args, input_text, timeout)

        with mock.patch.object(main, "run_capture", side_effect=old_pg), mock.patch("builtins.print"):
            main.teardown_db(argparse.Namespace())
        executed = fake.executed_sql()
        drop = executed.index("DROP DATABASE IF EXISTS vaulthalla;")
        terminate = next(i for i, s in enumerate(executed) if "pg_terminate_backend" in s)
        self.assertLess(terminate, drop)


class ServiceHealthTests(unittest.TestCase):
    def _props(self, sequence):
        items = iter(sequence)

        def fake(unit, *names):
            return next(items)

        return fake

    def test_healthy_when_active_and_stable_and_seed_consumed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            seed = Path(tmp) / "db_password"
            props = [{"NRestarts": "0"}] + [{"ActiveState": "active", "SubState": "running", "NRestarts": "0"}] * 20
            clock = iter(range(0, 100))
            with mock.patch.object(main, "PENDING_DB_PASSWORD_FILE", seed), \
                    mock.patch.object(main, "unit_properties", side_effect=self._props(props)), \
                    mock.patch.object(main.time, "monotonic", side_effect=lambda: float(next(clock))), \
                    mock.patch.object(main.time, "sleep"):
                ok, detail = main.wait_for_service_healthy(True, timeout=30, stable=5)
        self.assertTrue(ok, detail)

    def test_crash_loop_is_detected_by_restart_counter(self) -> None:
        props = [
            {"NRestarts": "28"},
            {"ActiveState": "activating", "SubState": "auto-restart", "NRestarts": "29"},
        ]
        with mock.patch.object(main, "unit_properties", side_effect=self._props(props)), \
                mock.patch.object(main.time, "sleep"):
            ok, detail = main.wait_for_service_healthy(False)
        self.assertFalse(ok)
        self.assertIn("restarts=29", detail)

    def test_restart_or_start_resets_failed_state_first(self) -> None:
        calls = []

        def fake(args, input_text=None, timeout=None):
            calls.append(list(args))
            return _done(returncode=3 if "is-active" in args else 0)

        with mock.patch.object(main, "command_exists", return_value=True), \
                mock.patch.object(main, "run_capture", side_effect=fake):
            self.assertEqual(main.restart_or_start_service(), "started")
        self.assertEqual(calls[0], ["systemctl", "reset-failed", main.SERVICE_UNIT])
        self.assertIn(["systemctl", "start", main.SERVICE_UNIT], calls)


class NginxDefaultSiteTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="vh-nginx-"))
        (self.tmp / "sites-available").mkdir()
        (self.tmp / "sites-enabled").mkdir()
        self.avail = self.tmp / "sites-available" / "default"
        self.enabled = self.tmp / "sites-enabled" / "default"
        self.marker = self.tmp / "nginx_default_site_disabled"
        self.avail.write_text("server { listen 80 default_server; }\n", encoding="utf-8")
        self.enabled.symlink_to(self.avail)
        for name, value in (
            ("NGINX_DEFAULT_SITE_AVAILABLE", self.avail),
            ("NGINX_DEFAULT_SITE_ENABLED", self.enabled),
            ("NGINX_DEFAULT_DISABLED_MARKER", self.marker),
        ):
            p = mock.patch.object(main, name, value)
            p.start()
            self.addCleanup(p.stop)

    def _md5(self) -> str:
        import hashlib

        return hashlib.md5(self.avail.read_bytes()).hexdigest()

    def test_disables_only_unmodified_stock_default_and_restores_it(self) -> None:
        with mock.patch.object(main, "dpkg_conffile_md5", return_value=self._md5()):
            self.assertTrue(main.disable_stock_nginx_default_site())
        self.assertFalse(self.enabled.is_symlink())
        self.assertTrue(self.marker.exists())
        self.assertTrue(main.restore_nginx_default_site())
        self.assertTrue(self.enabled.is_symlink())
        self.assertEqual(os.readlink(self.enabled), str(self.avail))
        self.assertFalse(self.marker.exists())

    def test_modified_default_site_is_never_touched(self) -> None:
        with mock.patch.object(main, "dpkg_conffile_md5", return_value="0" * 32):
            self.assertFalse(main.disable_stock_nginx_default_site())
        self.assertTrue(self.enabled.is_symlink())
        self.assertFalse(self.marker.exists())

    def test_conffile_md5_parses_dpkg_query_output(self) -> None:
        responses = {
            "-S": _done("nginx-common: /etc/nginx/sites-available/default\n"),
            "-W": _done(
                " /etc/nginx/nginx.conf 1111\n /etc/nginx/sites-available/default abcdef0123 \n"
                " /etc/nginx/sites-available/old 9999 obsolete\n"
            ),
        }

        def fake(args, input_text=None, timeout=None):
            return responses[args[1]]

        with mock.patch.object(main, "command_exists", return_value=True), \
                mock.patch.object(main, "run_capture", side_effect=fake):
            self.assertEqual(main.dpkg_conffile_md5(Path("/etc/nginx/sites-available/default")), "abcdef0123")
            self.assertIsNone(main.dpkg_conffile_md5(Path("/etc/nginx/sites-available/old")))


if __name__ == "__main__":
    unittest.main()
