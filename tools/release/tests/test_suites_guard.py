from __future__ import annotations

import io
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from tools.release.suites import SUITES, SuiteSpec, check_minimums, count_by_group, discover_suite, run_suites


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


_TEST_BODY = "import unittest\n\nclass T(unittest.TestCase):\n    def test_a(self):\n        pass\n\n    def test_b(self):\n        pass\n"


class SuiteGuardTests(unittest.TestCase):
    def _repo_root(self) -> Path:
        return Path(__file__).resolve().parents[3]

    def test_real_suites_meet_their_minimums(self) -> None:
        for name, spec in SUITES.items():
            with self.subTest(suite=name):
                counts = count_by_group(spec, discover_suite(spec, self._repo_root()))
                self.assertEqual(check_minimums(spec, counts), [])

    def test_packaging_directory_is_counted_as_its_own_group(self) -> None:
        spec = SUITES["release"]
        counts = count_by_group(spec, discover_suite(spec, self._repo_root()))
        self.assertGreaterEqual(counts["packaging"], spec.minimums["packaging"])

    def test_missing_init_py_is_detected_as_shortfall(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            _write(root / "suite" / "__init__.py", "")
            _write(root / "suite" / "test_root.py", _TEST_BODY)
            # No __init__.py: discover silently skips this directory on Python 3.12.
            _write(root / "suite" / "sub" / "test_sub.py", _TEST_BODY)
            spec = SuiteSpec(
                name="fixture",
                start_dir="suite",
                top_level_dir=None,
                subpackages=("sub",),
                minimums={"root": 2, "sub": 2},
            )
            counts = count_by_group(spec, discover_suite(spec, root))
            self.assertEqual(counts, {"root": 2, "sub": 0})
            self.assertEqual(
                check_minimums(spec, counts),
                ["fixture/sub: discovered 0 tests, expected at least 2"],
            )

            _write(root / "suite" / "sub" / "__init__.py", "")
            counts = count_by_group(spec, discover_suite(spec, root))
            self.assertEqual(check_minimums(spec, counts), [])

    def test_run_suites_rejects_unknown_names(self) -> None:
        with self.assertRaisesRegex(ValueError, "Unknown test suite"):
            _ = run_suites(["nope"], repo_root=self._repo_root(), stream=io.StringIO())

    def test_lifecycle_suite_counts_without_running(self) -> None:
        outcomes = run_suites(["lifecycle"], repo_root=self._repo_root(), count_only=True, stream=io.StringIO())
        self.assertEqual(outcomes[0].shortfalls, [])
        self.assertGreaterEqual(outcomes[0].counts["root"], 18)
        self.assertEqual(outcomes[0].ran, 0)


if __name__ == "__main__":
    unittest.main()
