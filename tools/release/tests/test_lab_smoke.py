from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from tools.release.lab_smoke import (
    LabSmoke,
    LabSmokeOptions,
    RemoteResult,
    SshLabHost,
    render_summary,
)
from tools.release.packaging.checksums import sha256_file
from tools.release.packaging.publication import LAB_SMOKE_EVIDENCE_SCHEMA

CONFIG = "/etc/vaulthalla/config.yaml"
PROVIDERS = "/etc/vaulthalla/testing/providers.env"


class FakeLab:
    """Scripted stand-in for `ssh <lab>`: a tiny state machine keyed on the commands lab-smoke sends."""

    def __init__(self) -> None:
        self.status, self.version = "ii ", "1.6.5-1"
        self.units = {
            "vaulthalla.service": {"LoadState": "loaded", "ActiveState": "active", "SubState": "running", "NRestarts": 0},
            "vaulthalla-web.service": {"LoadState": "loaded", "ActiveState": "active", "SubState": "running", "NRestarts": 0},
        }
        self.mounted = True
        self.waiting = 0
        self.files = {CONFIG: "c" * 64, PROVIDERS: "p" * 64}
        self.audit = ""
        self.lock_held = False
        self.vh_rc = 0
        self.cli_sock = True
        self.flapping_unit: str | None = None
        self.install_rewrites: str | None = None
        self.install_times_out = False
        self.pg_restart_wedges = False
        self.corrupt_copy = False
        self.down_polls = 0
        self.remote_files: dict[str, str] = {}
        self.commands: list[str] = []
        self.timeouts: list[int] = []

    def run(self, command: str, *, timeout: int) -> RemoteResult:
        self.commands.append(command)
        self.timeouts.append(timeout)
        if self.down_polls and command == "true":
            self.down_polls -= 1
            return RemoteResult(255, "", "ssh: connect to host lab port 22: Connection refused")
        if command.startswith("dpkg-query"):
            return RemoteResult(0, f"{self.status}|{self.version}" if self.version else "")
        if "dpkg --audit" in command:
            return RemoteResult(0, self.audit)
        if "fuser /var/lib/dpkg/lock-frontend" in command:
            return RemoteResult(0 if self.lock_held else 1)
        if command.startswith("if sudo -n test -e"):
            path = command.split("test -e ", 1)[1].split(";", 1)[0].strip("'")
            sha = self.files.get(path)
            return RemoteResult(0, f"{sha}  {path}\n" if sha else "absent\n")
        if command.startswith("sha256sum /tmp/vh-lab-smoke/"):
            path = command.split(" ", 1)[1].strip("'")
            return RemoteResult(0, f"{self.remote_files.get(path, '')}  {path}\n")
        if command.startswith("for u in"):
            if self.flapping_unit:
                self.units[self.flapping_unit]["NRestarts"] += 1
            out = []
            for unit in re.findall(r"for u in (.*?); do", command)[0].split():
                out.append(f"@@{unit}")
                for key, value in self.units.get(unit, {"LoadState": "not-found", "ActiveState": "inactive"}).items():
                    out.append(f"{key}={value}")
            return RemoteResult(0, "\n".join(out) + "\n")
        if command.startswith("awk"):
            line = "273 31 0:47 / /mnt/vaulthalla rw,nosuid shared:349 - fuse.vaulthalla-fuse vaulthalla-fuse rw\n"
            return RemoteResult(0, line if self.mounted else "")
        if "/sys/fs/fuse/connections/47/waiting" in command:
            return RemoteResult(0, f"{self.waiting}\n")
        if command.startswith("timeout 10 sudo -n stat"):
            return RemoteResult(0 if self.waiting == 0 else 124, "directory\n")
        if command.startswith("sudo -n test -S /run/vaulthalla/cli.sock"):
            return RemoteResult(0 if self.cli_sock else 1)
        if "vh status" in command:
            return RemoteResult(self.vh_rc)
        if "apt-get update" in command:
            return RemoteResult(0, "Reading package lists...")
        if "apt-get install" in command:
            if self.install_times_out:
                self.status = "iF "
                return RemoteResult(124, "Setting up vaulthalla ...", "", timed_out=True)
            target = command.rsplit(" ", 1)[1].strip("'")
            if target.startswith("vaulthalla="):
                self.version = target.split("=", 1)[1]
            else:
                self.version = Path(target).name.split("_")[1]
            self.status = "ii "
            if self.install_rewrites:
                self.files[self.install_rewrites] = "0" * 64
            return RemoteResult(0, "Setting up vaulthalla ...")
        if "systemctl restart postgresql" in command:
            if self.pg_restart_wedges:
                self.waiting = 3
                self.vh_rc = 124
            return RemoteResult(0)
        if "systemctl reboot" in command:
            self.down_polls = 2
            return RemoteResult(255, "", "Connection closed by remote host")
        if command == "true":
            return RemoteResult(0)
        if "is-system-running" in command:
            return RemoteResult(0, "running\n")
        if command.startswith("mkdir -p"):
            return RemoteResult(0)
        return RemoteResult(127, "", f"unexpected command: {command}")

    def copy_to(self, local: Path, remote_path: str, *, timeout: int) -> RemoteResult:
        self.remote_files[remote_path] = "f" * 64 if self.corrupt_copy else sha256_file(local)
        return RemoteResult(0)


class FakeClock:
    def __init__(self) -> None:
        self.now = 0.0

    def __call__(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


class LabSmokeTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = TemporaryDirectory()
        self.deb = Path(self._tmp.name) / "vaulthalla_1.6.7-1_amd64.deb"
        self.deb.write_bytes(b"candidate-deb-bytes")
        self.lab = FakeLab()
        self.clock = FakeClock()

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _run(self, **overrides) -> dict:
        options = LabSmokeOptions(host="lab", deb=self.deb, **overrides)
        smoke = LabSmoke(self.lab, options, sleep=self.clock.sleep, clock=self.clock, log=lambda _l: None)
        return smoke.run()

    def _failed(self, evidence: dict) -> list[str]:
        return [f"{p['name']}/{c['name']}" for p in evidence["phases"] for c in p["checks"] if not c["ok"]]

    def test_healthy_upgrade_passes_and_records_candidate_sha(self) -> None:
        evidence = self._run(from_version="1.6.5-1")

        self.assertTrue(evidence["ok"], self._failed(evidence))
        self.assertEqual(evidence["schema_version"], LAB_SMOKE_EVIDENCE_SCHEMA)
        self.assertEqual(evidence["candidate"]["sha256"], hashlib.sha256(b"candidate-deb-bytes").hexdigest())
        self.assertEqual(evidence["candidate"]["version"], "1.6.7-1")
        self.assertEqual([p["name"] for p in evidence["phases"]], ["baseline", "from:1.6.5-1", "candidate"])
        names = [c["name"] for c in evidence["phases"][-1]["checks"]]
        for expected in ("package_installed", "dpkg_audit_clean", "fuse_mounted", "fuse_waiting_zero",
                         "fuse_stat", "vh_status", f"unchanged:{CONFIG}", f"unchanged:{PROVIDERS}",
                         "cli_socket_bound", "legacy_unit_retired:vaulthalla-cli.socket",
                         "legacy_unit_retired:vaulthalla-cli.service"):
            self.assertIn(expected, names)
        self.assertEqual(self.lab.version, "1.6.7-1")
        # from-version already installed: no apt install for it, one for the candidate.
        self.assertEqual(sum("apt-get install" in c for c in self.lab.commands), 1)
        json.dumps(evidence)  # serializable
        self.assertIn("Result:     OK", render_summary(evidence))

    def test_every_remote_command_is_time_bounded_and_mount_touched_only_with_timeout(self) -> None:
        self._run(pg_restart=True, reboot=True)
        self.assertTrue(all(t > 0 for t in self.lab.timeouts))
        for command in self.lab.commands:
            if "/mnt/vaulthalla" in command and not command.startswith("awk"):
                self.assertTrue(command.startswith("timeout "), command)
            self.assertNotIn("pkill", command)
            self.assertNotIn("killall", command)
            self.assertNotRegex(command, r"(^|\s)df(\s|$)")

    def test_protected_file_rewritten_by_upgrade_fails(self) -> None:
        self.lab.install_rewrites = PROVIDERS
        evidence = self._run()
        self.assertFalse(evidence["ok"])
        self.assertIn(f"candidate/unchanged:{PROVIDERS}", self._failed(evidence))

    def test_absent_protected_file_is_not_required(self) -> None:
        del self.lab.files[PROVIDERS]
        evidence = self._run()
        self.assertTrue(evidence["ok"], self._failed(evidence))
        names = [c["name"] for c in evidence["phases"][-1]["checks"]]
        self.assertNotIn(f"unchanged:{PROVIDERS}", names)

    def test_restart_loop_is_detected(self) -> None:
        self.lab.flapping_unit = "vaulthalla-web.service"
        evidence = self._run()
        self.assertIn("candidate/unit_restarts_stable:vaulthalla-web.service", self._failed(evidence))

    def test_legacy_cli_socket_still_listening_fails_candidate(self) -> None:
        self.lab.units["vaulthalla-cli.socket"] = {
            "LoadState": "not-found", "ActiveState": "active", "SubState": "listening", "NRestarts": 0,
        }
        self.lab.cli_sock = False
        evidence = self._run(from_version="1.6.5-1")
        failed = self._failed(evidence)
        self.assertIn("candidate/legacy_unit_retired:vaulthalla-cli.socket", failed)
        self.assertIn("candidate/cli_socket_bound", failed)
        self.assertNotIn("candidate/legacy_unit_retired:vaulthalla-cli.service", failed)
        # The from-version phase still ships the legacy units and is not judged on them.
        self.assertFalse(any(name.startswith("from:1.6.5-1/legacy_unit_retired") for name in failed))

    def test_dirty_dpkg_baseline_aborts_before_installing(self) -> None:
        self.lab.status = "iF "
        self.lab.audit = "The following packages are only half configured"
        evidence = self._run()
        self.assertFalse(evidence["ok"])
        self.assertIn("refusing to install", evidence["failure"])
        self.assertFalse(any("apt-get install" in c for c in self.lab.commands))

    def test_install_timeout_is_reported(self) -> None:
        self.lab.install_times_out = True
        evidence = self._run()
        self.assertFalse(evidence["ok"])
        install = next(c for c in evidence["phases"][-1]["checks"] if c["name"] == "candidate:install")
        self.assertIn("TIMED OUT", install["detail"])

    def test_corrupted_transfer_aborts(self) -> None:
        self.lab.corrupt_copy = True
        evidence = self._run()
        self.assertIn("candidate transfer failed", evidence["failure"])
        self.assertFalse(any("apt-get install" in c for c in self.lab.commands))

    def test_postgres_restart_wedge_is_caught_without_touching_the_mount(self) -> None:
        self.lab.pg_restart_wedges = True
        evidence = self._run(pg_restart=True)
        failed = self._failed(evidence)
        self.assertIn("postgresql-restart/fuse_waiting_zero", failed)
        self.assertIn("postgresql-restart/vh_status", failed)
        pg_checks = {c["name"]: c for c in evidence["phases"][-1]["checks"]}
        self.assertIn("mount not touched", pg_checks["fuse_stat"]["detail"])

    def test_reboot_waits_for_ssh_and_reasserts(self) -> None:
        evidence = self._run(reboot=True)
        self.assertTrue(evidence["ok"], self._failed(evidence))
        self.assertEqual(evidence["phases"][-1]["name"], "reboot")

    def test_apt_candidate_mode_installs_pinned_version(self) -> None:
        options = LabSmokeOptions(host="lab", apt_version="1.6.6-1")
        evidence = LabSmoke(self.lab, options, sleep=self.clock.sleep, clock=self.clock, log=lambda _l: None).run()
        self.assertTrue(evidence["ok"], self._failed(evidence))
        self.assertTrue(any("vaulthalla='1.6.6-1'" in c or "vaulthalla=1.6.6-1" in c for c in self.lab.commands))

    def test_requires_exactly_one_candidate_source(self) -> None:
        with self.assertRaises(ValueError):
            LabSmoke(self.lab, LabSmokeOptions(host="lab"))
        with self.assertRaises(ValueError):
            LabSmoke(self.lab, LabSmokeOptions(host="lab", deb=self.deb, apt_version="1.0.0-1"))


class SshLabHostTests(unittest.TestCase):
    def test_remote_commands_are_wrapped_in_timeout_and_batch_mode(self) -> None:
        import subprocess

        with patch("tools.release.lab_smoke.subprocess.run",
                   return_value=subprocess.CompletedProcess([], 0, "ok", "")) as run:
            SshLabHost("vh-storage").run("sudo -n timeout 15 vh status", timeout=30)
        argv = run.call_args.args[0]
        self.assertEqual(argv[0], "ssh")
        self.assertIn("BatchMode=yes", argv)
        self.assertEqual(argv[-2], "vh-storage")
        self.assertEqual(argv[-1], "timeout -k 5 30 bash -c 'sudo -n timeout 15 vh status'")
        self.assertEqual(run.call_args.kwargs["timeout"], 60)

    def test_local_timeout_is_reported_as_timed_out(self) -> None:
        import subprocess

        with patch("tools.release.lab_smoke.subprocess.run", side_effect=subprocess.TimeoutExpired("ssh", 60)):
            result = SshLabHost("vh-storage").run("true", timeout=30)
        self.assertTrue(result.timed_out)
        self.assertEqual(result.returncode, 124)


if __name__ == "__main__":
    unittest.main()
