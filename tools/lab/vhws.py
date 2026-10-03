#!/usr/bin/env python3
"""Minimal Vaulthalla websocket client for web-console ("web-CLI") contract testing.

Requires: pip install websockets (e.g. in a venv).
Usage:
  vhws.py --url ws://10.0.0.33/ws --user admin --password-file F  CMD [JSON_PAYLOAD] [CMD2 JSON2 ...]
Logs in (auth.login) first, then sends each command in order on the same session and prints
one JSON line per response: {"command", "status", "data"/"error"}. Exit 1 if any response is an error.
Passwords are read from a file or VHWS_PASSWORD env and never printed.
"""
import argparse
import asyncio
import json
import os
import sys
import uuid

import websockets


async def run(args):
    password = os.environ.get("VHWS_PASSWORD")
    if args.password_file:
        with open(args.password_file) as fh:
            password = fh.read().strip()
    token = ""
    failed = False
    async with websockets.connect(args.url, open_timeout=10, close_timeout=5, max_size=2**24) as ws:
        async def send(command, payload):
            nonlocal token
            rid = str(uuid.uuid4())
            await ws.send(json.dumps({"command": command, "payload": payload, "requestId": rid, "token": token}))
            while True:
                raw = await asyncio.wait_for(ws.recv(), timeout=args.timeout)
                msg = json.loads(raw)
                if msg.get("requestId") != rid:
                    continue
                if msg.get("token"):
                    token = msg["token"]
                return msg

        steps = []
        if args.user:
            steps.append(("auth.login", {"name": args.user, "password": password}))
        rest = list(args.commands)
        while rest:
            cmd = rest.pop(0)
            payload = {}
            if rest and rest[0].lstrip().startswith("{"):
                payload = json.loads(rest.pop(0))
            steps.append((cmd, payload))
        for cmd, payload in steps:
            try:
                msg = await send(cmd, payload)
            except asyncio.TimeoutError:
                print(json.dumps({"command": cmd, "status": "TIMEOUT"}))
                failed = True
                continue
            status = msg.get("status")
            out = {"command": cmd, "status": status}
            if "error" in msg:
                out["error"] = msg["error"]
            if args.full or cmd != "auth.login":
                out["data"] = msg.get("data")
            print(json.dumps(out, default=str))
            if status not in ("ok", "OK", "success"):
                failed = True
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", required=True)
    ap.add_argument("--user")
    ap.add_argument("--password-file")
    ap.add_argument("--timeout", type=float, default=20)
    ap.add_argument("--full", action="store_true")
    ap.add_argument("commands", nargs="*")
    args = ap.parse_args()
    sys.exit(asyncio.run(run(args)))


if __name__ == "__main__":
    main()
