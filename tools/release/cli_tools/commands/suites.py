from __future__ import annotations

import argparse
from pathlib import Path

from tools.release.suites import SUITES, render_outcomes, run_suites


def cmd_run_tests(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    names = list(args.suite or SUITES)
    outcomes = run_suites(names, repo_root=repo_root, count_only=bool(args.count_only))
    print(render_outcomes(outcomes, count_only=bool(args.count_only)), end="")
    if args.count_only:
        return 0 if all(not outcome.shortfalls for outcome in outcomes) else 1
    return 0 if all(outcome.ok for outcome in outcomes) else 1
