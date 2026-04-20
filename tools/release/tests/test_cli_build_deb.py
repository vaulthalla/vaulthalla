from __future__ import annotations

import argparse
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
import unittest
from unittest.mock import patch

from tools.release import cli
from tools.release.packaging import DebianBuildResult


class CliBuildDebParsingTests(unittest.TestCase):
    def test_build_deb_parser_defaults(self) -> None:
        parser = cli.build_parser()
        parsed = parser.parse_args(["build-deb"])

        self.assertEqual(parsed.command, "build-deb")
        self.assertEqual(parsed.output_dir, "release")
        self.assertFalse(parsed.dry_run)
        self.assertTrue(callable(parsed.func))

    def test_build_deb_parser_flags(self) -> None:
        parser = cli.build_parser()
        parsed = parser.parse_args(["build-deb", "--output-dir", "/tmp/out", "--dry-run"])

        self.assertEqual(parsed.command, "build-deb")
        self.assertEqual(parsed.output_dir, "/tmp/out")
        self.assertTrue(parsed.dry_run)


class CliBuildDebCommandTests(unittest.TestCase):
    def _args(self, *, repo_root: str = ".", output_dir: str = "release", dry_run: bool = False) -> argparse.Namespace:
        return argparse.Namespace(repo_root=repo_root, output_dir=output_dir, dry_run=dry_run)

    def test_cmd_build_deb_dry_run_prints_plan(self) -> None:
        args = self._args(dry_run=True)
        out = StringIO()
        fake_result = DebianBuildResult(
            repo_root=Path("/tmp/repo"),
            output_dir=Path("/tmp/repo/release"),
            command=("dpkg-buildpackage", "-us", "-uc", "-b"),
            dry_run=True,
            package_name="vaulthalla",
            package_version="1.2.3-1",
            artifacts=(),
            build_log=None,
        )

        with (
            patch("tools.release.cli.build_debian_package", return_value=fake_result) as build_deb,
            redirect_stdout(out),
        ):
            rc = cli.cmd_build_deb(args)

        self.assertEqual(rc, 0)
        build_deb.assert_called_once()
        rendered = out.getvalue()
        self.assertIn("Debian build plan", rendered)
        self.assertIn("Dry run only", rendered)

    def test_cmd_build_deb_success_prints_artifacts(self) -> None:
        args = self._args(dry_run=False)
        out = StringIO()
        fake_result = DebianBuildResult(
            repo_root=Path("/tmp/repo"),
            output_dir=Path("/tmp/repo/release"),
            command=("dpkg-buildpackage", "-us", "-uc", "-b"),
            dry_run=False,
            package_name="vaulthalla",
            package_version="1.2.3-1",
            artifacts=(Path("/tmp/repo/release/vaulthalla_1.2.3-1_amd64.deb"),),
            build_log=Path("/tmp/repo/release/build-deb.log"),
        )

        with (
            patch("tools.release.cli.build_debian_package", return_value=fake_result),
            redirect_stdout(out),
        ):
            rc = cli.cmd_build_deb(args)

        self.assertEqual(rc, 0)
        rendered = out.getvalue()
        self.assertIn("Artifacts", rendered)
        self.assertIn("vaulthalla_1.2.3-1_amd64.deb", rendered)
        self.assertIn("build-deb.log", rendered)

    def test_main_reports_build_deb_error_cleanly(self) -> None:
        err = StringIO()
        with (
            patch("tools.release.cli.build_debian_package", side_effect=ValueError("build failed")),
            patch("sys.stderr", new=err),
        ):
            rc = cli.main(["build-deb"])

        self.assertEqual(rc, 1)
        self.assertIn("ERROR: build failed", err.getvalue())


if __name__ == "__main__":
    unittest.main()
