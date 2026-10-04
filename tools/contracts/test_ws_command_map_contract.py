"""Contract: every ws command core registers is typed in the web's WebSocketCommandMap, and nothing else is.

Core registers commands in core/src/protocols/ws/ (r->registerPayload("…") and friends); the console types every
request and response in web/src/util/webSocketCommands.ts `WebSocketCommandMap`. A command missing on either side
is a runtime "Unknown command" or an untyped call, so the two name sets must stay identical.

Run by module: python3 -m unittest tools.contracts.test_ws_command_map_contract
"""

from __future__ import annotations

from pathlib import Path
import re
import unittest

REPO_ROOT = Path(__file__).resolve().parents[2]
CORE_WS_DIR = REPO_ROOT / "core" / "src" / "protocols" / "ws"
WEB_COMMAND_MAP = REPO_ROOT / "web" / "src" / "util" / "webSocketCommands.ts"

REGISTER_RE = re.compile(
    r"\bregister(?:Payload|PayloadOnly|SessionOnlyHandler|HandlerWithToken|EmptyHandler|Ws|Handler)\s*\(\s*\"([^\"]+)\""
)
MAP_KEY_RE = re.compile(r"""\s*(?:'([^']+)'|"([^"]+)"|([A-Za-z_$][\w$]*))\??\s*:""")


def core_commands() -> dict[str, list[str]]:
    """Registered command name -> the files registering it."""
    found: dict[str, list[str]] = {}
    for path in sorted(CORE_WS_DIR.rglob("*.cpp")):
        for name in REGISTER_RE.findall(path.read_text(encoding="utf-8")):
            found.setdefault(name, []).append(str(path.relative_to(REPO_ROOT)))
    return found


def _strip_comments_and_strings(source: str) -> str:
    """Blank out comments and the contents of string literals so brace counting sees only structure. Quotes and
    newlines stay in place, so key names (quoted) are still readable from the original text at the same offsets."""
    out = list(source)
    i, n = 0, len(source)
    while i < n:
        c = source[i]
        if source.startswith("//", i):
            j = source.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
        elif source.startswith("/*", i):
            j = source.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif c in "'\"`":
            j = i + 1
            while j < n and source[j] != c:
                j += 2 if source[j] == "\\" else 1
            for k in range(i + 1, min(j, n)):
                out[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(out)


def web_commands() -> list[str]:
    """Top-level keys of `export interface WebSocketCommandMap { ... }`, in source order."""
    source = WEB_COMMAND_MAP.read_text(encoding="utf-8")
    start = source.index("export interface WebSocketCommandMap")
    open_brace = source.index("{", start)
    structural = _strip_comments_and_strings(source)

    keys: list[str] = []
    depth = 0
    i = open_brace
    line_start = True
    while i < len(source):
        c = structural[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return keys
        elif c == "\n":
            line_start = True
            i += 1
            continue
        if depth == 1 and line_start and not c.isspace():
            m = MAP_KEY_RE.match(source, i)
            if m:
                keys.append(next(g for g in m.groups() if g))
        if not c.isspace():
            line_start = False
        i += 1
    raise AssertionError("unterminated WebSocketCommandMap in " + str(WEB_COMMAND_MAP.relative_to(REPO_ROOT)))


class WsCommandMapContractTests(unittest.TestCase):
    def test_extractors_see_the_command_sets(self) -> None:
        core = core_commands()
        web = web_commands()
        # Both sides are large; a parser that silently finds nothing must not pass as "in sync".
        self.assertGreater(len(core), 100, "found too few core ws registrations; did registration move?")
        self.assertGreater(len(web), 100, "found too few WebSocketCommandMap keys; did the map move?")
        self.assertIn("auth.login", core)
        self.assertIn("auth.login", web)

    def test_no_command_is_registered_or_typed_twice(self) -> None:
        dupes = {name: files for name, files in core_commands().items() if len(files) > 1}
        self.assertFalse(dupes, f"ws commands registered more than once in core: {dupes}")
        web = web_commands()
        web_dupes = sorted({name for name in web if web.count(name) > 1})
        self.assertFalse(web_dupes, f"WebSocketCommandMap keys declared twice: {web_dupes}")

    def test_core_registrations_and_web_command_map_match(self) -> None:
        core = set(core_commands())
        web = set(web_commands())
        missing_in_web = sorted(core - web)
        missing_in_core = sorted(web - core)
        problems = []
        if missing_in_web:
            problems.append(
                "registered in core but missing from web/src/util/webSocketCommands.ts WebSocketCommandMap: "
                + ", ".join(missing_in_web)
            )
        if missing_in_core:
            problems.append(
                "typed in WebSocketCommandMap but not registered in core/src/protocols/ws: " + ", ".join(missing_in_core)
            )
        self.assertFalse(problems, "\n".join(problems))


if __name__ == "__main__":
    unittest.main()
