"""Fast Python test suites with minimum-count guards.

`unittest discover` silently skips directories that are not packages (P0-4: 69 packaging tests were
never run while CI reported OK). Every suite here declares a floor per test group; if discovery finds
fewer tests than the floor (a lost `__init__.py`, a renamed directory, an import-time skip), the run
fails before any test executes. Raise a floor when you add tests; lower it only deliberately.
"""

from __future__ import annotations

import io
import sys
import unittest
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, TextIO


@dataclass(frozen=True)
class SuiteSpec:
    name: str
    start_dir: str
    top_level_dir: str | None
    minimums: dict[str, int]
    # Dotted prefix stripped from test ids before grouping (start dir relative to top level).
    id_prefix: str = ""
    subpackages: tuple[str, ...] = field(default_factory=tuple)

    def group_of(self, test_id: str) -> str:
        remainder = test_id[len(self.id_prefix):] if self.id_prefix and test_id.startswith(self.id_prefix) else test_id
        head = remainder.split(".", 1)[0]
        return head if head in self.subpackages else "root"


SUITES: dict[str, SuiteSpec] = {
    "release": SuiteSpec(
        name="release",
        start_dir="tools/release/tests",
        top_level_dir=None,
        subpackages=("changelog", "packaging"),
        minimums={"changelog": 229, "packaging": 103, "root": 85},
    ),
    "lifecycle": SuiteSpec(
        name="lifecycle",
        start_dir="deploy/lifecycle/tests",
        top_level_dir=".",
        id_prefix="deploy.lifecycle.tests.",
        minimums={"root": 18},
    ),
    "lab": SuiteSpec(
        name="lab",
        start_dir="tools/lab/tests",
        top_level_dir=".",
        id_prefix="tools.lab.tests.",
        minimums={"root": 6},
    ),
}


@dataclass
class SuiteOutcome:
    name: str
    counts: dict[str, int]
    shortfalls: list[str]
    ran: int = 0
    failures: int = 0
    errors: int = 0
    skipped: int = 0

    @property
    def ok(self) -> bool:
        return not self.shortfalls and self.failures == 0 and self.errors == 0


def _iter_tests(suite: unittest.TestSuite) -> Iterable[unittest.TestCase]:
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from _iter_tests(item)
        else:
            yield item


def discover_suite(spec: SuiteSpec, repo_root: Path) -> unittest.TestSuite:
    root = repo_root.resolve()
    if str(root) not in sys.path:
        sys.path.insert(0, str(root))
    top = str(root / spec.top_level_dir) if spec.top_level_dir is not None else None
    return unittest.TestLoader().discover(str(root / spec.start_dir), pattern="test_*.py", top_level_dir=top)


def count_by_group(spec: SuiteSpec, suite: unittest.TestSuite) -> dict[str, int]:
    counts: dict[str, int] = {group: 0 for group in spec.minimums}
    for test in _iter_tests(suite):
        group = spec.group_of(test.id())
        counts[group] = counts.get(group, 0) + 1
    return counts


def check_minimums(spec: SuiteSpec, counts: dict[str, int]) -> list[str]:
    return [
        f"{spec.name}/{group}: discovered {counts.get(group, 0)} tests, expected at least {minimum}"
        for group, minimum in sorted(spec.minimums.items())
        if counts.get(group, 0) < minimum
    ]


def run_suites(
    names: Iterable[str],
    *,
    repo_root: Path,
    count_only: bool = False,
    stream: TextIO | None = None,
) -> list[SuiteOutcome]:
    out = sys.stderr if stream is None else stream
    outcomes: list[SuiteOutcome] = []
    for name in names:
        if name not in SUITES:
            raise ValueError(f"Unknown test suite `{name}`. Known: {', '.join(sorted(SUITES))}.")
        spec = SUITES[name]
        if not (repo_root / spec.start_dir).is_dir():
            # A missing suite directory is exactly the silent-skip this guard exists to catch.
            outcomes.append(
                SuiteOutcome(
                    name=name,
                    counts={group: 0 for group in spec.minimums},
                    shortfalls=[f"{spec.name}: start directory {spec.start_dir} does not exist"],
                )
            )
            continue
        suite = discover_suite(spec, repo_root)
        counts = count_by_group(spec, suite)
        outcome = SuiteOutcome(name=name, counts=counts, shortfalls=check_minimums(spec, counts))
        if not outcome.shortfalls and not count_only:
            result = unittest.TextTestRunner(stream=out, verbosity=1).run(suite)
            outcome.ran = result.testsRun
            outcome.failures = len(result.failures)
            outcome.errors = len(result.errors)
            outcome.skipped = len(result.skipped)
        outcomes.append(outcome)
    return outcomes


def render_outcomes(outcomes: list[SuiteOutcome], *, count_only: bool = False) -> str:
    buffer = io.StringIO()
    buffer.write("Test suites\n-----------\n")
    for outcome in outcomes:
        counts = ", ".join(f"{group}={count}" for group, count in sorted(outcome.counts.items()))
        status = "OK" if outcome.ok else "FAILED"
        if count_only:
            status = "OK" if not outcome.shortfalls else "FAILED"
            buffer.write(f"{outcome.name:<10} discovered [{counts}] {status}\n")
        else:
            buffer.write(
                f"{outcome.name:<10} discovered [{counts}] ran={outcome.ran} failures={outcome.failures} "
                f"errors={outcome.errors} skipped={outcome.skipped} {status}\n"
            )
        for shortfall in outcome.shortfalls:
            buffer.write(f"  !! {shortfall}\n")
    return buffer.getvalue()
