#!/usr/bin/env python3
"""Check the package build log: every project C++ compile ran at -O3 with Debian's hardening flags and -Werror,
and the compiler printed no warning.

Packaged binaries once shipped unoptimized and unhardened: a cpp_args default_option made meson drop the flags
dpkg-buildflags exported, and nothing looked at the compile lines. This reads them (dh runs ninja verbosely).

    python3 tools/dev/check_build_flags.py release/build-deb.log
"""

from __future__ import annotations

import re
import shlex
import sys
from pathlib import Path

REQUIRED = ("-O3", "-Werror", "-fstack-protector-strong", "-D_FORTIFY_SOURCE=3", "-g")
OPTIMIZATION = re.compile(r"^-O(\d|s|g|z|fast)?$")
COMPILE = re.compile(r"(?:^|\s)(?:\S*/)?(?:c\+\+|g\+\+)(?:-\d+)?\s.*\s-c\s")
DIAGNOSTIC = re.compile(r"^\S+:\d+:\d+: (?:warning|error):|^/usr/bin/ld: warning:|^collect2: error:")


def check(log: str) -> list[str]:
    problems: list[str] = []
    compiles = 0
    for number, line in enumerate(log.splitlines(), 1):
        if DIAGNOSTIC.search(line):
            problems.append(f"line {number}: compiler diagnostic: {line.strip()[:200]}")
            continue
        if not COMPILE.search(line) or "/core/" not in line:
            continue
        compiles += 1
        args = shlex.split(line[line.index("c++") if "c++" in line else 0:], posix=True)
        levels = [a for a in args if OPTIMIZATION.match(a)]
        missing = [flag for flag in REQUIRED if flag not in args]
        if levels and levels[-1] != "-O3":
            problems.append(f"line {number}: effective optimization {levels[-1]} (expected -O3)")
        if missing:
            problems.append(f"line {number}: missing {' '.join(missing)}")
    if compiles == 0:
        problems.append("no project compile commands found (is the log from a verbose dh build?)")
    return problems


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    problems = check(Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace"))
    for problem in problems[:50]:
        print(problem, file=sys.stderr)
    if problems:
        print(f"check_build_flags: {len(problems)} problem(s)", file=sys.stderr)
        return 1
    print("check_build_flags: every project compile at -O3 with hardening and -Werror; no compiler diagnostics")
    return 0


if __name__ == "__main__":
    sys.exit(main())
