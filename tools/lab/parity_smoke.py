#!/usr/bin/env python3
"""CLI <-> web-console (websocket) parity smoke against a packaged Vaulthalla host.

Each scenario performs an operation through one surface and verifies the resulting state through the
other, and checks that both surfaces *reject* invalid operations (duplicates, missing entities) instead
of reporting success. Nothing here stress-tests providers; it only creates/deletes `parity-*` objects.

Requires: `pip install websockets`; an ssh alias with passwordless sudo on the host; the web admin's
(non-default) password in a 0600 file.

  parity_smoke.py --host vh-storage --ws-url ws://10.0.0.33/ws --admin-password-file F [--evidence out.json]

Exit status: 0 when every check passed, 1 otherwise. Passwords are never printed.
"""
from __future__ import annotations

import argparse
import asyncio
import json
import shlex
import subprocess
import sys
import time
import uuid
from dataclasses import asdict, dataclass, field

import websockets


@dataclass
class Check:
    scenario: str
    name: str
    ok: bool
    detail: str = ""


@dataclass
class Report:
    checks: list[Check] = field(default_factory=list)

    def add(self, scenario: str, name: str, ok: bool, detail: str = "") -> bool:
        self.checks.append(Check(scenario, name, bool(ok), detail[:400]))
        print(f"[{'PASS' if ok else 'FAIL'}] {scenario}: {name}" + (f" -- {detail[:200]}" if detail and not ok else ""))
        return bool(ok)


class Cli:
    def __init__(self, host: str) -> None:
        self.host = host

    def run(self, *args: str, timeout: int = 30) -> tuple[int, str]:
        remote = "sudo timeout {} vh {}".format(timeout, " ".join(shlex.quote(a) for a in args))
        proc = subprocess.run(["ssh", "-o", "BatchMode=yes", self.host, remote],
                              capture_output=True, text=True, timeout=timeout + 20)
        return proc.returncode, (proc.stdout + proc.stderr)


class Ws:
    def __init__(self, url: str) -> None:
        self.url = url
        self.token = ""
        self.conn = None

    async def __aenter__(self) -> "Ws":
        self.conn = await websockets.connect(self.url, open_timeout=10, close_timeout=5, max_size=2**24)
        return self

    async def __aexit__(self, *exc) -> None:
        await self.conn.close()

    async def send(self, command: str, payload=None, timeout: float = 20) -> dict:
        rid = str(uuid.uuid4())
        await self.conn.send(json.dumps({"command": command, "payload": payload if payload is not None else {},
                                         "requestId": rid, "token": self.token}))
        while True:
            msg = json.loads(await asyncio.wait_for(self.conn.recv(), timeout=timeout))
            if msg.get("requestId") != rid:
                continue
            if msg.get("token"):
                self.token = msg["token"]
            return msg


def ok_status(msg: dict) -> bool:
    return str(msg.get("status", "")).upper() in ("OK", "SUCCESS")


def names(items, key="name") -> set[str]:
    return {i.get(key) for i in (items or []) if isinstance(i, dict)}


async def scenarios(cli: Cli, ws: Ws, r: Report) -> None:
    tag = time.strftime("%H%M%S")

    # --- vaults ---------------------------------------------------------------
    s = "vault"
    v_cli, v_ws = f"parity-cli-{tag}", f"parity-ws-{tag}"
    rc, out = cli.run("vault", "create", v_cli, "--local", "--desc", "parity", "--quota", "1G")
    r.add(s, "CLI create succeeds", rc == 0, out)
    lst = await ws.send("storage.vault.list")
    r.add(s, "ws list sees CLI-created vault", v_cli in names(lst.get("data", {}).get("vaults")), json.dumps(lst)[:300])
    rc, out = cli.run("vault", "create", v_cli, "--local")
    r.add(s, "CLI duplicate create is rejected (non-zero)", rc != 0, out)
    add = await ws.send("storage.vault.add", {"name": v_ws, "type": "local", "mount_point": v_ws})
    r.add(s, "ws create succeeds", ok_status(add), json.dumps(add)[:300])
    dup = await ws.send("storage.vault.add", {"name": v_ws, "type": "local", "mount_point": v_ws})
    r.add(s, "ws duplicate create is rejected", not ok_status(dup), json.dumps(dup)[:300])
    rc, out = cli.run("vaults")
    r.add(s, "CLI list sees ws-created vault", rc == 0 and v_ws in out, out[-300:])
    lst = await ws.send("storage.vault.list")
    ids = {v.get("name"): v.get("id") for v in lst.get("data", {}).get("vaults", [])}
    rc, out = cli.run("vault", "delete", v_ws, "--owner", "admin")  # names are per-owner, so --owner is required
    r.add(s, "CLI deletes ws-created vault", rc == 0, out)
    if ids.get(v_cli) is not None:
        rm = await ws.send("storage.vault.remove", {"id": ids[v_cli]})
        r.add(s, "ws deletes CLI-created vault", ok_status(rm), json.dumps(rm)[:300])
    rc, out = cli.run("vaults")
    r.add(s, "both vaults gone (CLI view)", v_cli not in out and v_ws not in out, out[-300:])
    rm = await ws.send("storage.vault.remove", {"id": 987654})
    r.add(s, "ws delete of missing vault is rejected", not ok_status(rm), json.dumps(rm)[:300])
    rc, out = cli.run("vault", "delete", "parity-does-not-exist")
    r.add(s, "CLI delete of missing vault is rejected (non-zero)", rc != 0, out)

    # --- groups ---------------------------------------------------------------
    s = "group"
    g_cli, g_ws = f"parity-g-cli-{tag}", f"parity-g-ws-{tag}"
    rc, out = cli.run("group", "create", g_cli)
    r.add(s, "CLI create succeeds", rc == 0, out)
    lst = await ws.send("groups.list")
    r.add(s, "ws list sees CLI-created group", g_cli in names(lst.get("data", {}).get("groups")), json.dumps(lst)[:300])
    add = await ws.send("group.add", {"name": g_ws})
    r.add(s, "ws create succeeds", ok_status(add), json.dumps(add)[:300])
    dup = await ws.send("group.add", {"name": g_ws})
    r.add(s, "ws duplicate create is rejected", not ok_status(dup), json.dumps(dup)[:300])
    rc, out = cli.run("group", "create", g_ws)
    r.add(s, "CLI duplicate of ws-created group is rejected", rc != 0, out)
    rc, out = cli.run("group", "delete", g_ws)
    r.add(s, "CLI deletes ws-created group", rc == 0, out)
    lst = await ws.send("groups.list")
    gid = {g.get("name"): g.get("id") for g in lst.get("data", {}).get("groups", [])}.get(g_cli)
    if gid is not None:
        rm = await ws.send("group.remove", {"id": gid})
        r.add(s, "ws deletes CLI-created group", ok_status(rm), json.dumps(rm)[:300])
    rm = await ws.send("group.remove", {"id": 987654})
    r.add(s, "ws delete of missing group is rejected", not ok_status(rm), json.dumps(rm)[:300])
    rc, out = cli.run("group", "info", "parity-no-such-group")
    r.add(s, "CLI info of missing group is rejected and daemon survives", rc != 0, out)
    rc, _ = cli.run("status")
    r.add(s, "daemon healthy after group error paths", rc == 0)

    # --- users ----------------------------------------------------------------
    s = "user"
    u_ws, u_cli = f"parity_u_ws_{tag}", f"parity_u_cli_{tag}"
    reg = await ws.send("auth.register", {"name": u_ws, "email": f"{u_ws}@example.invalid",
                                          "password": "Zq9#" + uuid.uuid4().hex + "!Xw", "is_active": True,
                                          "role": "unprivileged"})
    r.add(s, "ws register succeeds", ok_status(reg), json.dumps(reg)[:300])
    rc, out = cli.run("user", "info", u_ws)
    r.add(s, "CLI sees ws-registered user", rc == 0 and u_ws in out, out[-300:])
    rc, out = cli.run("user", "create", u_cli, "--role", "unprivileged")
    r.add(s, "CLI create succeeds", rc == 0, "exit %d" % rc)  # output contains a generated password; not logged
    got = await ws.send("auth.user.get.byName", {"name": u_cli})
    uid = (got.get("data") or {}).get("user", {}).get("id")
    r.add(s, "ws sees CLI-created user", ok_status(got) and uid is not None, json.dumps(got)[:200])
    if uid is not None:
        upd = await ws.send("auth.user.update", {"id": uid, "name": u_cli, "email": f"{u_cli}@example.invalid",
                                                "is_active": True, "role": "unprivileged"})
        r.add(s, "ws edit targets the selected user (#126)", ok_status(upd) and
              (upd.get("data") or {}).get("user", {}).get("id") == uid, json.dumps(upd)[:300])
        me = await ws.send("auth.isAuthenticated", {"token": ws.token})
        r.add(s, "editing another user leaves the caller unchanged (#126)",
              (me.get("data") or {}).get("user", {}).get("name") == "admin", json.dumps(me)[:200])
        dele = await ws.send("auth.user.delete", {"id": uid})
        r.add(s, "ws deletes CLI-created user", ok_status(dele), json.dumps(dele)[:300])
    rc, out = cli.run("user", "delete", u_ws)
    r.add(s, "CLI deletes ws-registered user", rc == 0, out)
    rc, out = cli.run("user", "info", u_ws)
    r.add(s, "deleted user is gone (CLI)", rc != 0, out)
    self_role = await ws.send("auth.user.update", {"role": "unprivileged"})
    r.add(s, "ws self role change is rejected", not ok_status(self_role), json.dumps(self_role)[:300])


async def main_async(args) -> int:
    with open(args.admin_password_file) as fh:
        password = fh.read().strip()
    cli, report = Cli(args.host), Report()
    async with Ws(args.ws_url) as ws:
        login = await ws.send("auth.login", {"name": args.admin_user, "password": password})
        if not report.add("setup", "ws admin login", ok_status(login), str(login.get("error", ""))):
            return 1
        await scenarios(cli, ws, report)
    failed = [c for c in report.checks if not c.ok]
    print(f"\n{len(report.checks) - len(failed)}/{len(report.checks)} checks passed")
    if args.evidence:
        with open(args.evidence, "w") as fh:
            json.dump({"host": args.host, "ws_url": args.ws_url, "checks": [asdict(c) for c in report.checks]}, fh, indent=2)
    return 0 if not failed else 1


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--host", required=True)
    ap.add_argument("--ws-url", required=True)
    ap.add_argument("--admin-user", default="admin")
    ap.add_argument("--admin-password-file", required=True)
    ap.add_argument("--evidence")
    sys.exit(asyncio.run(main_async(ap.parse_args())))


if __name__ == "__main__":
    main()
