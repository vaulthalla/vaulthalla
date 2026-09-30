"""Real-host install/upgrade smoke test for a candidate package (`python3 -m tools.release lab-smoke`).

Everything runs over `ssh <host>`; every remote command is wrapped in `timeout` and every local
ssh/scp has its own timeout. Hazard rules (see .claude/skills/lab/SKILL.md):
- never kill by substring; this module never kills anything,
- the FUSE mount is detected via /proc/self/mountinfo and fusectl counters, and is only touched
  (`stat`) with a timeout and only after the kernel reports no waiting requests,
- never a bare `df` (not used here).
This MUTATES the target host (apt install, optional PostgreSQL restart / reboot): point it only at a lab.
"""

from __future__ import annotations

import json
import re
import shlex
import subprocess
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Protocol

from tools.release.packaging.checksums import sha256_file
from tools.release.packaging.publication import LAB_SMOKE_EVIDENCE_SCHEMA, identify_debian_artifact

PACKAGE = "vaulthalla"
MOUNTPOINT = "/mnt/vaulthalla"
FUSE_FSTYPE = "fuse.vaulthalla-fuse"
CONFIG_PATH = "/etc/vaulthalla/config.yaml"
# Operator-owned files that an install/upgrade must never rewrite. Compared by sha256 (read via sudo,
# contents never printed); a file absent before the upgrade is not checked.
PROTECTED_FILES: tuple[str, ...] = (CONFIG_PATH, "/etc/vaulthalla/testing/providers.env")
REMOTE_STAGING_DIR = "/tmp/vh-lab-smoke"
DEFAULT_UNITS: tuple[str, ...] = (
    "vaulthalla.service",
    "vaulthalla-web.service",
)
# Shipped up to 1.6.6. The socket unit was an orphaned listener on the daemon's CLI socket path
# (`vh` hung forever); postinst retires both on upgrade and the daemon owns the socket (#110).
LEGACY_CLI_UNITS: tuple[str, ...] = ("vaulthalla-cli.socket", "vaulthalla-cli.service")
CLI_SOCKET_PATH = "/run/vaulthalla/cli.sock"
APT_ENV = "DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=l"
APT_OPTS = "-y -q -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold"


@dataclass(frozen=True)
class RemoteResult:
    returncode: int
    stdout: str = ""
    stderr: str = ""
    timed_out: bool = False


class LabHost(Protocol):
    def run(self, command: str, *, timeout: int) -> RemoteResult: ...

    def copy_to(self, local: Path, remote_path: str, *, timeout: int) -> RemoteResult: ...


class SshLabHost:
    """`ssh <host> timeout -k 5 N bash -c '<cmd>'`, with a local timeout as a second bound."""

    SSH_OPTIONS: tuple[str, ...] = (
        "-o", "BatchMode=yes",
        "-o", "ConnectTimeout=10",
        "-o", "ServerAliveInterval=15",
        "-o", "ServerAliveCountMax=4",
    )

    def __init__(self, host: str) -> None:
        self.host = host

    def _exec(self, argv: list[str], timeout: int) -> RemoteResult:
        try:
            completed = subprocess.run(argv, text=True, capture_output=True, check=False, timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            return RemoteResult(124, _text(exc.stdout), _text(exc.stderr), timed_out=True)
        return RemoteResult(completed.returncode, completed.stdout, completed.stderr, timed_out=completed.returncode == 124)

    def run(self, command: str, *, timeout: int) -> RemoteResult:
        remote = f"timeout -k 5 {int(timeout)} bash -c {shlex.quote(command)}"
        return self._exec(["ssh", *self.SSH_OPTIONS, self.host, remote], timeout + 30)

    def copy_to(self, local: Path, remote_path: str, *, timeout: int) -> RemoteResult:
        return self._exec(["scp", "-q", *self.SSH_OPTIONS, str(local), f"{self.host}:{remote_path}"], timeout)


def _text(value: object) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)


@dataclass
class Check:
    name: str
    ok: bool
    detail: str = ""


@dataclass
class Phase:
    name: str
    checks: list[Check] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return all(check.ok for check in self.checks)

    def add(self, name: str, ok: bool, detail: str = "") -> Check:
        check = Check(name=name, ok=bool(ok), detail=detail)
        self.checks.append(check)
        return check


@dataclass(frozen=True)
class LabSmokeOptions:
    host: str
    deb: Path | None = None
    apt_version: str | None = None
    from_version: str | None = None
    units: tuple[str, ...] = DEFAULT_UNITS
    pg_restart: bool = False
    reboot: bool = False
    settle_seconds: int = 15
    ready_timeout: int = 180
    install_timeout: int = 900
    reboot_timeout: int = 600


class LabSmokeAbort(RuntimeError):
    pass


class LabSmoke:
    def __init__(
        self,
        host: LabHost,
        options: LabSmokeOptions,
        *,
        sleep: Callable[[float], None] = time.sleep,
        clock: Callable[[], float] = time.monotonic,
        log: Callable[[str], None] = print,
    ) -> None:
        if (options.deb is None) == (options.apt_version is None):
            raise ValueError("Exactly one of --deb or --apt-version is required.")
        self.host = host
        self.options = options
        self.sleep = sleep
        self.clock = clock
        self.log = log
        self.phases: list[Phase] = []
        self.evidence: dict = {}

    # --- remote probes ------------------------------------------------------------------------

    def sh(self, command: str, timeout: int = 30) -> RemoteResult:
        return self.host.run(command, timeout=timeout)

    def package_state(self) -> tuple[str, str]:
        result = self.sh(f"dpkg-query -W -f='${{db:Status-Abbrev}}|${{Version}}' {PACKAGE} 2>/dev/null || true")
        raw = result.stdout.strip()
        if "|" not in raw:
            return "not-installed", ""
        status, version = raw.split("|", 1)
        return status.strip(), version.strip()

    def dpkg_audit(self) -> RemoteResult:
        return self.sh("sudo -n dpkg --audit", timeout=60)

    def dpkg_lock_free(self) -> bool:
        return self.sh("sudo -n fuser /var/lib/dpkg/lock-frontend >/dev/null 2>&1").returncode != 0

    def file_sha(self, path: str) -> str | None:
        quoted = shlex.quote(path)
        result = self.sh(f"if sudo -n test -e {quoted}; then sudo -n sha256sum {quoted}; else echo absent; fi")
        token = result.stdout.strip().split()[0] if result.stdout.strip() else ""
        return None if token in ("", "absent") or result.returncode != 0 else token

    def protected_shas(self) -> dict[str, str | None]:
        return {path: self.file_sha(path) for path in PROTECTED_FILES}

    def unit_states(self, units_to_check: tuple[str, ...] | None = None) -> dict[str, dict[str, str]]:
        units = " ".join(shlex.quote(unit) for unit in (units_to_check or self.options.units))
        result = self.sh(
            f'for u in {units}; do echo "@@$u"; '
            'systemctl show "$u" -p LoadState -p ActiveState -p SubState -p NRestarts 2>/dev/null; done'
        )
        states: dict[str, dict[str, str]] = {}
        current: str | None = None
        for line in result.stdout.splitlines():
            if line.startswith("@@"):
                current = line[2:].strip()
                states[current] = {}
            elif current and "=" in line:
                key, value = line.split("=", 1)
                states[current][key.strip()] = value.strip()
        return states

    def fuse_mount(self) -> tuple[str | None, str | None]:
        """(fstype, connection minor) from /proc/self/mountinfo, never touching the mount itself."""
        result = self.sh(f"awk '$5==\"{MOUNTPOINT}\"' /proc/self/mountinfo")
        line = result.stdout.strip().splitlines()[0] if result.stdout.strip() else ""
        if not line:
            return None, None
        fields = line.split()
        minor = fields[2].split(":")[1] if len(fields) > 2 and ":" in fields[2] else None
        fstype = None
        if " - " in line:
            fstype = line.split(" - ", 1)[1].split()[0]
        return fstype, minor

    def fuse_waiting(self, minor: str) -> int | None:
        if not re.fullmatch(r"\d+", minor):
            return None
        result = self.sh(f"sudo -n cat /sys/fs/fuse/connections/{minor}/waiting")
        raw = result.stdout.strip()
        return int(raw) if result.returncode == 0 and raw.isdigit() else None

    # --- phases -------------------------------------------------------------------------------

    def wait_ready(self, phase: Phase) -> None:
        deadline = self.clock() + self.options.ready_timeout
        last = ""
        while True:
            states = self.unit_states()
            core = states.get(self.options.units[0], {})
            fstype, _minor = self.fuse_mount()
            if core.get("ActiveState") == "active" and fstype == FUSE_FSTYPE:
                phase.add("ready", True, f"{self.options.units[0]} active and {MOUNTPOINT} mounted")
                return
            last = f"{self.options.units[0]}={core.get('ActiveState', '?')} mount={fstype or 'absent'}"
            if self.clock() >= deadline:
                phase.add("ready", False, f"not ready after {self.options.ready_timeout}s: {last}")
                return
            self.sleep(5)

    def assert_healthy(
        self,
        phase: Phase,
        *,
        expected_version: str,
        protected_before: dict[str, str | None] | None,
        candidate: bool = True,
    ) -> None:
        status, version = self.package_state()
        phase.add("package_installed", status == "ii" and version == expected_version,
                  f"{PACKAGE} {status} {version or '-'} (expected ii {expected_version})")
        audit = self.dpkg_audit()
        phase.add("dpkg_audit_clean", audit.returncode == 0 and not audit.stdout.strip(),
                  audit.stdout.strip()[:500] or "empty")

        self.wait_ready(phase)
        first = self.unit_states()
        self.sleep(self.options.settle_seconds)
        second = self.unit_states()
        for unit in self.options.units:
            before, after = first.get(unit, {}), second.get(unit, {})
            active = after.get("ActiveState") == "active"
            phase.add(f"unit_active:{unit}", active,
                      f"{after.get('LoadState', '?')}/{after.get('ActiveState', '?')}/{after.get('SubState', '?')}")
            stable = before.get("NRestarts") is not None and before.get("NRestarts") == after.get("NRestarts")
            phase.add(f"unit_restarts_stable:{unit}", stable,
                      f"NRestarts {before.get('NRestarts', '?')} -> {after.get('NRestarts', '?')} "
                      f"over {self.options.settle_seconds}s")

        fstype, minor = self.fuse_mount()
        phase.add("fuse_mounted", fstype == FUSE_FSTYPE, f"{MOUNTPOINT} fstype={fstype or 'absent'}")
        waiting = self.fuse_waiting(minor) if minor else None
        if waiting is not None and waiting > 0:
            self.sleep(2)
            waiting = self.fuse_waiting(minor) if minor else None
        phase.add("fuse_waiting_zero", waiting == 0, f"fusectl conn {minor or '?'} waiting={waiting}")
        if waiting == 0:
            stat = self.sh(f"timeout 10 stat -c %F {MOUNTPOINT}", timeout=20)
            phase.add("fuse_stat", stat.returncode == 0,
                      stat.stdout.strip() or stat.stderr.strip()[:200] or f"exit {stat.returncode}")
        else:
            phase.add("fuse_stat", False, "skipped: FUSE requests pending (mount not touched)")
        if candidate:
            legacy = self.unit_states(LEGACY_CLI_UNITS)
            for unit in LEGACY_CLI_UNITS:
                state = legacy.get(unit, {})
                retired = state.get("LoadState") == "not-found" and state.get("ActiveState") in ("inactive", None)
                phase.add(f"legacy_unit_retired:{unit}", retired,
                          f"{state.get('LoadState', '?')}/{state.get('ActiveState', '?')} (expected not-found/inactive)")
            sock = self.sh(f"sudo -n test -S {CLI_SOCKET_PATH}")
            phase.add("cli_socket_bound", sock.returncode == 0,
                      f"{CLI_SOCKET_PATH} {'is a socket' if sock.returncode == 0 else 'missing'}")
        vh = self.sh("sudo -n timeout 15 vh status", timeout=30)
        phase.add("vh_status", vh.returncode == 0,
                  f"exit {vh.returncode}{' (timed out)' if vh.returncode == 124 else ''}")
        for path, before in (protected_before or {}).items():
            if before is None:
                continue
            after_sha = self.file_sha(path)
            phase.add(f"unchanged:{path}", after_sha == before,
                      f"sha256 {before[:12]} -> {(after_sha or 'absent')[:12]}")

    def apt_install(self, phase: Phase, target: str, *, label: str, update: bool) -> bool:
        if update:
            upd = self.sh(f"sudo -n env {APT_ENV} apt-get update -q", timeout=300)
            phase.add(f"{label}:apt_update", upd.returncode == 0, f"exit {upd.returncode}")
            if upd.returncode != 0:
                return False
        result = self.sh(
            f"sudo -n env {APT_ENV} apt-get install {APT_OPTS} --allow-downgrades {target}",
            timeout=self.options.install_timeout,
        )
        detail = f"exit {result.returncode}" + (" (TIMED OUT: dpkg may be left half-configured)" if result.timed_out
                                                else "")
        tail = "\n".join((result.stdout + result.stderr).strip().splitlines()[-15:])
        phase.add(f"{label}:install", result.returncode == 0 and not result.timed_out,
                  f"{detail}\n{tail}".strip())
        return result.returncode == 0 and not result.timed_out

    def run(self) -> dict:
        started = datetime.now(timezone.utc).isoformat()
        options = self.options
        candidate: dict = {"source": "deb" if options.deb else "apt"}
        if options.deb is not None:
            identity = identify_debian_artifact(options.deb)
            candidate.update(
                path=str(options.deb), name=options.deb.name, version=identity.version, sha256=identity.sha256
            )
        else:
            candidate.update(version=options.apt_version, sha256=None)
        expected_version = str(candidate["version"])
        failure: str | None = None
        baseline: dict = {}

        try:
            pre = Phase("baseline")
            self.phases.append(pre)
            status, version = self.package_state()
            baseline["package"] = {"status": status, "version": version}
            baseline["protected_sha256"] = self.protected_shas()
            baseline["units"] = self.unit_states()
            audit = self.dpkg_audit()
            pre.add("dpkg_audit_clean", audit.returncode == 0 and not audit.stdout.strip(),
                    audit.stdout.strip()[:500] or "empty")
            pre.add("dpkg_lock_free", self.dpkg_lock_free(), "/var/lib/dpkg/lock-frontend")
            pre.add("package_state", status in ("ii", "not-installed"), f"{status} {version}".strip())
            if not pre.ok:
                raise LabSmokeAbort("host is not in a clean dpkg state; refusing to install on top of it")

            if options.from_version:
                start = Phase(f"from:{options.from_version}")
                self.phases.append(start)
                if version != options.from_version or status != "ii":
                    if not self.apt_install(start, f"{PACKAGE}={shlex.quote(options.from_version)}",
                                            label="from", update=True):
                        raise LabSmokeAbort(f"installing from-version {options.from_version} failed")
                else:
                    start.add("from:already_installed", True, f"{PACKAGE} {version}")
                self.assert_healthy(start, expected_version=options.from_version, protected_before=None,
                                    candidate=False)
                if not start.ok:
                    raise LabSmokeAbort(f"from-version {options.from_version} is not healthy; upgrade result would be meaningless")

            protected_before = self.protected_shas()
            upgrade = Phase("candidate")
            self.phases.append(upgrade)
            if options.deb is not None:
                remote_path = f"{REMOTE_STAGING_DIR}/{options.deb.name}"
                mk = self.sh(f"mkdir -p {REMOTE_STAGING_DIR} && chmod 0755 {REMOTE_STAGING_DIR}")
                copy = self.host.copy_to(options.deb, remote_path, timeout=600) if mk.returncode == 0 else mk
                upgrade.add("candidate:copied", copy.returncode == 0, f"{remote_path} exit {copy.returncode}")
                remote_sum = self.sh(f"sha256sum {shlex.quote(remote_path)}", timeout=60).stdout.split()
                remote_sha = remote_sum[0] if remote_sum else ""
                upgrade.add("candidate:sha256_matches", remote_sha == candidate["sha256"],
                            f"remote {remote_sha[:12] or '-'} local {candidate['sha256'][:12]}")
                if not upgrade.ok:
                    raise LabSmokeAbort("candidate transfer failed")
                installed = self.apt_install(upgrade, shlex.quote(remote_path), label="candidate", update=False)
            else:
                installed = self.apt_install(
                    upgrade, f"{PACKAGE}={shlex.quote(str(options.apt_version))}", label="candidate", update=True
                )
                cached = self.sh(
                    f"sha256sum /var/cache/apt/archives/{PACKAGE}_{str(options.apt_version).replace(':', '%3a')}_*.deb "
                    "2>/dev/null | head -1"
                ).stdout.split()
                candidate["sha256"] = cached[0] if cached else None
            if not installed:
                raise LabSmokeAbort("candidate install failed")
            self.assert_healthy(upgrade, expected_version=expected_version, protected_before=protected_before)

            if options.pg_restart:
                pg = Phase("postgresql-restart")
                self.phases.append(pg)
                restart = self.sh("sudo -n systemctl restart postgresql", timeout=120)
                pg.add("postgresql_restarted", restart.returncode == 0, f"exit {restart.returncode}")
                self.sleep(self.options.settle_seconds)
                self.assert_healthy(pg, expected_version=expected_version, protected_before=protected_before)

            if options.reboot:
                rb = Phase("reboot")
                self.phases.append(rb)
                self.sh("sudo -n systemctl reboot", timeout=20)  # the connection drops; exit status is meaningless
                self.sleep(20)
                deadline = self.clock() + options.reboot_timeout
                back = False
                while self.clock() < deadline:
                    if self.sh("true", timeout=15).returncode == 0:
                        back = True
                        break
                    self.sleep(10)
                rb.add("host_back", back, f"ssh reachable within {options.reboot_timeout}s" if back else "timed out")
                if not back:
                    raise LabSmokeAbort("host did not come back after reboot")
                system = self.sh("systemctl is-system-running --wait", timeout=300)
                state = system.stdout.strip() or "unknown"
                rb.add("system_running", state in ("running", "degraded"), state)
                self.assert_healthy(rb, expected_version=expected_version, protected_before=protected_before)
        except LabSmokeAbort as exc:
            failure = str(exc)

        ok = failure is None and all(phase.ok for phase in self.phases)
        if failure is None and not ok:
            failed = [f"{p.name}/{c.name}" for p in self.phases for c in p.checks if not c.ok]
            failure = "failed checks: " + ", ".join(failed)
        self.evidence = {
            "schema_version": LAB_SMOKE_EVIDENCE_SCHEMA,
            "ok": ok,
            "host": options.host,
            "started_at": started,
            "finished_at": datetime.now(timezone.utc).isoformat(),
            "candidate": candidate,
            "from_version": options.from_version,
            "options": {
                "units": list(options.units),
                "pg_restart": options.pg_restart,
                "reboot": options.reboot,
                "settle_seconds": options.settle_seconds,
                "install_timeout": options.install_timeout,
            },
            "baseline": baseline,
            "phases": [{"name": p.name, "ok": p.ok, "checks": [asdict(c) for c in p.checks]} for p in self.phases],
            "failure": failure,
        }
        return self.evidence


def render_summary(evidence: dict) -> str:
    lines = [
        "Lab smoke",
        "---------",
        f"Host:       {evidence.get('host')}",
        f"Candidate:  {evidence['candidate'].get('name') or evidence['candidate'].get('version')} "
        f"sha256={evidence['candidate'].get('sha256') or '-'}",
        f"From:       {evidence.get('from_version') or '(installed state)'}",
    ]
    for phase in evidence.get("phases", []):
        lines.append(f"[{'PASS' if phase['ok'] else 'FAIL'}] {phase['name']}")
        for check in phase["checks"]:
            detail = check["detail"].splitlines()[0] if check["detail"] else ""
            lines.append(f"    {'ok  ' if check['ok'] else 'FAIL'} {check['name']}: {detail}")
    lines.append(f"Result:     {'OK' if evidence['ok'] else 'FAILED'}")
    if evidence.get("failure"):
        lines.append(f"Failure:    {evidence['failure']}")
    return "\n".join(lines) + "\n"


def main_lab_smoke(args) -> int:
    deb = Path(args.deb).resolve() if args.deb else None
    if deb is not None and not deb.is_file():
        raise ValueError(f"--deb not found: {deb}")
    options = LabSmokeOptions(
        host=args.host,
        deb=deb,
        apt_version=args.apt_version,
        from_version=args.from_version,
        units=tuple(args.unit) if args.unit else DEFAULT_UNITS,
        pg_restart=bool(args.pg_restart),
        reboot=bool(args.reboot),
        settle_seconds=int(args.settle_seconds),
        install_timeout=int(args.install_timeout),
    )
    smoke = LabSmoke(SshLabHost(args.host), options)
    evidence = smoke.run()
    if args.evidence:
        target = Path(args.evidence)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(render_summary(evidence), end="")
    if args.evidence:
        print(f"Evidence:   {args.evidence}")
    return 0 if evidence["ok"] else 1


__all__ = [
    "DEFAULT_UNITS",
    "LabHost",
    "LabSmoke",
    "LabSmokeOptions",
    "RemoteResult",
    "SshLabHost",
    "main_lab_smoke",
    "render_summary",
    "sha256_file",
]
