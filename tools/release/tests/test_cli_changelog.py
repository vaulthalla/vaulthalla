from __future__ import annotations

import argparse
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from tools.release import cli
from tools.release.version.models import Version


class CliChangelogDraftTests(unittest.TestCase):
    def _args(
        self,
        *,
        repo_root: str = ".",
        fmt: str = "raw",
        since_tag: str | None = None,
        output: str | None = None,
    ) -> argparse.Namespace:
        return argparse.Namespace(
            repo_root=repo_root,
            format=fmt,
            since_tag=since_tag,
            output=output,
        )

    def test_changelog_draft_raw_to_stdout(self) -> None:
        args = self._args(fmt="raw")
        out = StringIO()

        with (
            patch("tools.release.cli.read_version_file", return_value=Version(1, 2, 3)) as read_version,
            patch("tools.release.cli.build_release_context", return_value=object()) as build_context,
            patch("tools.release.cli.render_release_changelog", return_value="# Release Draft\n") as render_raw,
            patch("tools.release.cli.render_debug_json") as render_json,
            redirect_stdout(out),
        ):
            result = cli.cmd_changelog_draft(args)

        self.assertEqual(result, 0)
        self.assertEqual(out.getvalue(), "# Release Draft\n")
        read_version.assert_called_once()
        build_context.assert_called_once()
        kwargs = build_context.call_args.kwargs
        self.assertEqual(kwargs["version"], "1.2.3")
        self.assertIsNone(kwargs["previous_tag"])
        render_raw.assert_called_once()
        render_json.assert_not_called()

    def test_changelog_draft_json_to_stdout(self) -> None:
        args = self._args(fmt="json")
        out = StringIO()

        with (
            patch("tools.release.cli.read_version_file", return_value=Version(0, 28, 1)),
            patch("tools.release.cli.build_release_context", return_value=object()),
            patch("tools.release.cli.render_release_changelog") as render_raw,
            patch("tools.release.cli.render_debug_json", return_value='{"ok":true}') as render_json,
            redirect_stdout(out),
        ):
            result = cli.cmd_changelog_draft(args)

        self.assertEqual(result, 0)
        self.assertEqual(out.getvalue(), '{"ok":true}\n')
        render_json.assert_called_once()
        render_raw.assert_not_called()

    def test_since_tag_override_is_forwarded(self) -> None:
        args = self._args(since_tag="v0.27.0")
        out = StringIO()

        with (
            patch("tools.release.cli.read_version_file", return_value=Version(1, 0, 0)),
            patch("tools.release.cli.build_release_context", return_value=object()) as build_context,
            patch("tools.release.cli.render_release_changelog", return_value="# Draft\n"),
            redirect_stdout(out),
        ):
            result = cli.cmd_changelog_draft(args)

        self.assertEqual(result, 0)
        self.assertEqual(build_context.call_args.kwargs["previous_tag"], "v0.27.0")

    def test_output_file_writing(self) -> None:
        with TemporaryDirectory() as temp_dir:
            target = Path(temp_dir) / "release.md"
            args = self._args(output=str(target))
            out = StringIO()

            with (
                patch("tools.release.cli.read_version_file", return_value=Version(2, 0, 0)),
                patch("tools.release.cli.build_release_context", return_value=object()),
                patch("tools.release.cli.render_release_changelog", return_value="# File Draft\n"),
                redirect_stdout(out),
            ):
                result = cli.cmd_changelog_draft(args)

            self.assertEqual(result, 0)
            self.assertTrue(target.is_file())
            self.assertEqual(target.read_text(encoding="utf-8"), "# File Draft\n")
            self.assertIn("Wrote changelog draft to", out.getvalue())

    def test_existing_version_commands_still_parse(self) -> None:
        parser = cli.build_parser()

        for argv in (
            ["check"],
            ["sync"],
            ["set-version", "1.2.3"],
            ["bump", "patch"],
        ):
            parsed = parser.parse_args(argv)
            self.assertTrue(callable(parsed.func))

    def test_new_command_surface_parses(self) -> None:
        parser = cli.build_parser()

        parsed = parser.parse_args(["changelog", "draft"])
        self.assertEqual(parsed.command, "changelog")
        self.assertEqual(parsed.changelog_command, "draft")
        self.assertEqual(parsed.format, "raw")

        parsed_json = parser.parse_args(
            ["changelog", "draft", "--format", "json", "--since-tag", "v0.1.0", "--output", "/tmp/x.md"]
        )
        self.assertEqual(parsed_json.format, "json")
        self.assertEqual(parsed_json.since_tag, "v0.1.0")
        self.assertEqual(parsed_json.output, "/tmp/x.md")


if __name__ == "__main__":
    unittest.main()
