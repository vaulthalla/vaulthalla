# tools/release/tests/helpers/cli_harness.py

from __future__ import annotations

from contextlib import ExitStack, redirect_stdout
from dataclasses import dataclass
from io import StringIO
from pathlib import Path
from types import SimpleNamespace
from typing import Any
from unittest.mock import Mock, patch


@dataclass(frozen=True)
class DraftPolicy:
    ratio: float = 0.35
    min: int = 800
    max: int = 4000


class CliHarness:
    def __init__(self, test):
        self.test = test
        self.stack = ExitStack()
        self.out = StringIO()
        self.err = StringIO()
        self.mocks = SimpleNamespace()
        self.provider = object()

    def __enter__(self):
        self.stack.__enter__()
        self.stack.enter_context(redirect_stdout(self.out))
        return self

    def __exit__(self, *exc):
        return self.stack.__exit__(*exc)

    def patch(self, path: str, **kwargs) -> Mock:
        mock = self.stack.enter_context(patch(path, **kwargs))
        setattr(self.mocks, path.split(".")[-1], mock)
        return mock

    def patch_stderr(self):
        return self.patch("sys.stderr", new=self.err)

    def stdout(self) -> str:
        return self.out.getvalue()

    def stderr(self) -> str:
        return self.err.getvalue()

    def assert_stdout_contains(self, text: str):
        self.test.assertIn(text, self.stdout())

    def assert_stderr_contains(self, text: str):
        self.test.assertIn(text, self.stderr())

    def assert_file_contains(self, path: Path, text: str):
        self.test.assertTrue(path.is_file(), f"Expected file to exist: {path}")
        self.test.assertIn(text, path.read_text(encoding="utf-8"))

    def assert_file_equals(self, path: Path, text: str):
        self.test.assertTrue(path.is_file(), f"Expected file to exist: {path}")
        self.test.assertEqual(path.read_text(encoding="utf-8"), text)
