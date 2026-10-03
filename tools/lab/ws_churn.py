#!/usr/bin/env python3
"""WebSocket connection-churn smoke for a packaged Vaulthalla host.

Runs concurrent clients through the connection lifecycles that exercise server-side closes
(logout, bad-login rate limiting, sweeper idle timeout, abrupt client disconnects, garbage frames)
and asserts the daemon survives: same MainPID, NRestarts unchanged, `vh status` healthy.
Regression guard for #127 / #129 (handler exceptions and close/read races aborting the daemon).

  ws_churn.py --host vh-storage --ws-url ws://10.0.0.33/ws --admin-password-file F [--rounds 5] [--idle]

Requires `pip install websockets`. Passwords are read from a file and never printed.
"""
from __future__ import annotations

import argparse
import asyncio
import json
import subprocess
import sys
import uuid

import websockets


ADMIN_USER = "admin"


def daemon_state(host: str) -> tuple[str, str]:
    out = subprocess.run(["ssh", "-o", "BatchMode=yes", host,
                          "systemctl show vaulthalla -p MainPID --value; systemctl show vaulthalla -p NRestarts --value"],
                         capture_output=True, text=True, timeout=30).stdout.split()
    return (out + ["?", "?"])[0], (out + ["?", "?"])[1]


async def call(ws, command, payload=None, token=""):
    rid = str(uuid.uuid4())
    await ws.send(json.dumps({"command": command, "payload": payload or {}, "requestId": rid, "token": token}))
    while True:
        msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=20))
        if msg.get("requestId") == rid:
            return msg


async def login_logout(url, password):
    async with websockets.connect(url, open_timeout=10) as ws:
        msg = await call(ws, "auth.login", {"name": ADMIN_USER, "password": password})
        token = msg.get("token", "")
        await call(ws, "storage.vault.list", None, token)
        await call(ws, "auth.logout", None, token)


async def abrupt_disconnect(url, password):
    ws = await websockets.connect(url, open_timeout=10)
    rid = str(uuid.uuid4())
    await ws.send(json.dumps({"command": "auth.login", "payload": {"name": ADMIN_USER, "password": password},
                              "requestId": rid, "token": ""}))
    ws.transport.abort()  # drop the TCP connection while the server is answering


async def garbage(url, _password):
    async with websockets.connect(url, open_timeout=10) as ws:
        await ws.send("{not json")
        await ws.send(b"\x00\x01binary-without-upload")
        await asyncio.wait_for(ws.recv(), timeout=20)


async def bad_logins(url, _password):
    async with websockets.connect(url, open_timeout=10) as ws:
        for _ in range(3):
            await call(ws, "auth.login", {"name": "churn-nobody", "password": "wrong"})


async def idle_until_swept(url, _password):
    async with websockets.connect(url, open_timeout=10) as ws:
        try:
            while True:
                await asyncio.wait_for(ws.recv(), timeout=150)
        except (websockets.ConnectionClosed, asyncio.TimeoutError):
            return


async def run(args) -> int:
    global ADMIN_USER
    ADMIN_USER = args.admin_user
    with open(args.admin_password_file) as fh:
        password = fh.read().strip()
    pid0, restarts0 = daemon_state(args.host)
    print(f"daemon before: pid={pid0} restarts={restarts0}")
    flows = [login_logout, abrupt_disconnect, garbage, bad_logins]
    errors = 0
    for rnd in range(args.rounds):
        tasks = [flow(args.ws_url, password) for flow in flows for _ in range(args.concurrency)]
        if args.idle and rnd == 0:
            tasks += [idle_until_swept(args.ws_url, password) for _ in range(args.concurrency)]
        results = await asyncio.gather(*tasks, return_exceptions=True)
        errs = [r for r in results if isinstance(r, Exception) and not isinstance(r, websockets.ConnectionClosed)]
        errors += len(errs)
        pid, restarts = daemon_state(args.host)
        print(f"round {rnd + 1}: {len(tasks)} clients, {len(errs)} client-side errors, daemon pid={pid} restarts={restarts}")
        for e in errs[:3]:
            print(f"   client error: {type(e).__name__}: {str(e)[:160]}")
        if pid != pid0 or restarts != restarts0:
            print("FAIL: daemon restarted during churn")
            return 1
    status = subprocess.run(["ssh", "-o", "BatchMode=yes", args.host, "sudo timeout 20 vh status >/dev/null"],
                            timeout=60).returncode
    print(f"vh status rc={status}")
    return 0 if status == 0 and errors == 0 else 1


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--host", required=True)
    ap.add_argument("--ws-url", required=True)
    ap.add_argument("--admin-user", default="admin")
    ap.add_argument("--admin-password-file", required=True)
    ap.add_argument("--rounds", type=int, default=5)
    ap.add_argument("--concurrency", type=int, default=4)
    ap.add_argument("--idle", action="store_true", help="also hold unauthenticated connections until the sweeper closes them")
    sys.exit(asyncio.run(run(ap.parse_args())))


if __name__ == "__main__":
    main()
