from __future__ import annotations

import argparse
from contextlib import redirect_stdout
from io import StringIO
import json
import os
from pathlib import Path
from tempfile import TemporaryDirectory
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import call, patch

from tools.release import cli
from tools.release.changelog.models import CategoryContext, CommitInfo, DiffSnippet, FileChange, ReleaseContext
from tools.release.changelog.ai.config import AIDynamicRatioTokenBudget, DEFAULT_AI_DRAFT_MODEL
from tools.release.changelog.ai.providers import ProviderPreflightResult
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
            patch("tools.release.cli.build_changelog_context", return_value=object()) as build_context,
            patch("tools.release.cli.render_release_changelog", return_value="# Release Draft\n") as render_raw,
            patch("tools.release.cli.render_debug_json") as render_json,
            redirect_stdout(out),
        ):
            result = cli.cmd_changelog_draft(args)

        self.assertEqual(result, 0)
        self.assertEqual(out.getvalue(), "# Release Draft\n")
        build_context.assert_called_once()
        self.assertEqual(build_context.call_args.args[1], None)
        render_raw.assert_called_once()
        render_json.assert_not_called()

    def test_changelog_draft_json_to_stdout(self) -> None:
        args = self._args(fmt="json")
        out = StringIO()

        with (
            patch("tools.release.cli.build_changelog_context", return_value=object()),
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
            patch("tools.release.cli.build_changelog_context", return_value=object()) as build_context,
            patch("tools.release.cli.render_release_changelog", return_value="# Draft\n"),
            redirect_stdout(out),
        ):
            result = cli.cmd_changelog_draft(args)

        self.assertEqual(result, 0)
        self.assertEqual(build_context.call_args.args[1], "v0.27.0")

    def test_output_file_writing(self) -> None:
        with TemporaryDirectory() as temp_dir:
            target = Path(temp_dir) / "release.md"
            args = self._args(output=str(target))
            out = StringIO()

            with (
                patch("tools.release.cli.build_changelog_context", return_value=object()),
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

        parsed_payload = parser.parse_args(
            ["changelog", "payload", "--since-tag", "v0.1.0", "--output", "/tmp/payload.json"]
        )
        self.assertEqual(parsed_payload.command, "changelog")
        self.assertEqual(parsed_payload.changelog_command, "payload")
        self.assertEqual(parsed_payload.since_tag, "v0.1.0")
        self.assertEqual(parsed_payload.output, "/tmp/payload.json")

        parsed_release = parser.parse_args(
            [
                "changelog",
                "release",
                "--since-tag",
                "v0.1.0",
                "--output",
                "/tmp/release.md",
                "--raw-output",
                "/tmp/raw.md",
                "--payload-output",
                "/tmp/release-payload.json",
                "--semantic-payload-output",
                "/tmp/release-semantic-payload.json",
                "--manual-changelog-path",
                "debian/changelog",
            ]
        )
        self.assertEqual(parsed_release.command, "changelog")
        self.assertEqual(parsed_release.changelog_command, "release")
        self.assertEqual(parsed_release.since_tag, "v0.1.0")
        self.assertEqual(parsed_release.output, "/tmp/release.md")
        self.assertEqual(parsed_release.raw_output, "/tmp/raw.md")
        self.assertEqual(parsed_release.payload_output, "/tmp/release-payload.json")
        self.assertEqual(parsed_release.semantic_payload_output, "/tmp/release-semantic-payload.json")
        self.assertEqual(parsed_release.manual_changelog_path, "debian/changelog")
        self.assertEqual(parsed_release.cached_draft_path, ".changelog_scratch/changelog.draft.md")
        self.assertIsNone(parsed_release.debian_distribution)
        self.assertIsNone(parsed_release.debian_urgency)

        parsed_ai_release = parser.parse_args(
            [
                "changelog",
                "ai-release",
                "--ai-profile",
                "openai-balanced",
                "--draft-output",
                "/tmp/draft.md",
                "--output",
                "/tmp/release.md",
            ]
        )
        self.assertEqual(parsed_ai_release.command, "changelog")
        self.assertEqual(parsed_ai_release.changelog_command, "ai-release")
        self.assertEqual(parsed_ai_release.ai_profile, "openai-balanced")
        self.assertEqual(parsed_ai_release.draft_output, "/tmp/draft.md")
        self.assertEqual(parsed_ai_release.output, "/tmp/release.md")
        self.assertEqual(
            parsed_ai_release.semantic_payload_output,
            ".changelog_scratch/changelog.semantic_payload.json",
        )

        parsed_ai_check = parser.parse_args(
            [
                "changelog",
                "ai-check",
                "--ai-profile",
                "local-gemma",
                "--provider",
                "openai-compatible",
                "--base-url",
                "http://localhost:8888/v1",
                "--model",
                "Qwen3.5-122B",
            ]
        )
        self.assertEqual(parsed_ai_check.command, "changelog")
        self.assertEqual(parsed_ai_check.changelog_command, "ai-check")
        self.assertEqual(parsed_ai_check.ai_profile, "local-gemma")
        self.assertEqual(parsed_ai_check.provider, "openai-compatible")
        self.assertEqual(parsed_ai_check.base_url, "http://localhost:8888/v1")
        self.assertEqual(parsed_ai_check.model, "Qwen3.5-122B")

        parsed_ai = parser.parse_args(
            [
                "changelog",
                "ai-draft",
                "--since-tag",
                "v0.1.0",
                "--output",
                "/tmp/ai.md",
                "--save-json",
                "/tmp/ai.json",
                "--ai-profile",
                "openai-balanced",
                "--model",
                "gpt-5.4-mini",
                "--provider",
                "openai-compatible",
                "--base-url",
                "http://localhost:8888/v1",
                "--use-triage",
                "--save-triage-json",
                "/tmp/triage.json",
                "--polish",
                "--save-polish-json",
                "/tmp/polish.json",
            ]
        )
        self.assertEqual(parsed_ai.command, "changelog")
        self.assertEqual(parsed_ai.changelog_command, "ai-draft")
        self.assertEqual(parsed_ai.since_tag, "v0.1.0")
        self.assertEqual(parsed_ai.output, "/tmp/ai.md")
        self.assertEqual(parsed_ai.save_json, "/tmp/ai.json")
        self.assertEqual(parsed_ai.ai_profile, "openai-balanced")
        self.assertEqual(parsed_ai.model, "gpt-5.4-mini")
        self.assertEqual(parsed_ai.provider, "openai-compatible")
        self.assertEqual(parsed_ai.base_url, "http://localhost:8888/v1")
        self.assertTrue(parsed_ai.use_triage)
        self.assertEqual(parsed_ai.save_triage_json, "/tmp/triage.json")
        self.assertTrue(parsed_ai.polish)
        self.assertEqual(parsed_ai.save_polish_json, "/tmp/polish.json")

        parsed_ai_compare = parser.parse_args(
            [
                "changelog",
                "ai-compare",
                "--since-tag",
                "v0.1.0",
                "--ai-profiles",
                "openai-cheap,openai-balanced,openai-premium",
                "--output-name",
                "0.34.0-openai-comparison.md",
            ]
        )
        self.assertEqual(parsed_ai_compare.command, "changelog")
        self.assertEqual(parsed_ai_compare.changelog_command, "ai-compare")
        self.assertEqual(parsed_ai_compare.since_tag, "v0.1.0")
        self.assertEqual(
            parsed_ai_compare.ai_profiles,
            "openai-cheap,openai-balanced,openai-premium",
        )
        self.assertEqual(parsed_ai_compare.output_name, "0.34.0-openai-comparison.md")


class CliChangelogPayloadTests(unittest.TestCase):
    def _args(
        self,
        *,
        repo_root: str = ".",
        since_tag: str | None = None,
        output: str | None = None,
    ) -> argparse.Namespace:
        return argparse.Namespace(
            repo_root=repo_root,
            since_tag=since_tag,
            output=output,
        )

    def test_payload_to_stdout(self) -> None:
        args = self._args(since_tag="v0.1.0")
        out = StringIO()

        with (
            patch("tools.release.cli.build_changelog_context", return_value=object()) as build_context,
            patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}) as build_payload,
            patch("tools.release.cli.render_ai_payload_json", return_value='{"schema_version":"x"}\n') as render_payload,
            redirect_stdout(out),
        ):
            result = cli.cmd_changelog_payload(args)

        self.assertEqual(result, 0)
        self.assertEqual(out.getvalue(), '{"schema_version":"x"}\n')
        self.assertEqual(build_context.call_args.args[1], "v0.1.0")
        build_payload.assert_called_once()
        render_payload.assert_called_once()

    def test_payload_to_output_file(self) -> None:
        with TemporaryDirectory() as temp_dir:
            target = Path(temp_dir) / "payload.json"
            args = self._args(output=str(target))
            out = StringIO()

            with (
                patch("tools.release.cli.build_changelog_context", return_value=object()),
                patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
                patch("tools.release.cli.render_ai_payload_json", return_value='{"schema_version":"x"}\n'),
                redirect_stdout(out),
            ):
                result = cli.cmd_changelog_payload(args)

            self.assertEqual(result, 0)
            self.assertTrue(target.is_file())
            self.assertEqual(target.read_text(encoding="utf-8"), '{"schema_version":"x"}\n')
            self.assertIn("Wrote changelog payload to", out.getvalue())


class CliChangelogReleaseTests(unittest.TestCase):
    def _args(
        self,
        *,
        repo_root: str = ".",
        since_tag: str | None = None,
        output: str = ".changelog_scratch/changelog.release.md",
        raw_output: str = ".changelog_scratch/changelog.raw.md",
        payload_output: str = ".changelog_scratch/changelog.payload.json",
        semantic_payload_output: str = ".changelog_scratch/changelog.semantic_payload.json",
        release_notes_output: str = ".changelog_scratch/release_notes.md",
        manual_changelog_path: str = "debian/changelog",
        cached_draft_path: str = ".changelog_scratch/changelog.draft.md",
        debian_distribution: str | None = None,
        debian_urgency: str | None = None,
    ) -> argparse.Namespace:
        return argparse.Namespace(
            repo_root=repo_root,
            since_tag=since_tag,
            output=output,
            raw_output=raw_output,
            payload_output=payload_output,
            semantic_payload_output=semantic_payload_output,
            release_notes_output=release_notes_output,
            manual_changelog_path=manual_changelog_path,
            cached_draft_path=cached_draft_path,
            debian_distribution=debian_distribution,
            debian_urgency=debian_urgency,
        )

    def test_release_command_writes_evidence_and_selected_changelog(self) -> None:
        with TemporaryDirectory() as temp_dir:
            target = Path(temp_dir) / "release.md"
            raw_target = Path(temp_dir) / "raw.md"
            payload_target = Path(temp_dir) / "payload.json"
            semantic_payload_target = Path(temp_dir) / "semantic-payload.json"
            args = self._args(
                output=str(target),
                raw_output=str(raw_target),
                payload_output=str(payload_target),
                semantic_payload_output=str(semantic_payload_target),
            )
            out = StringIO()

            with (
                patch("tools.release.cli.build_changelog_context", return_value=object()),
                patch("tools.release.cli.render_release_changelog", return_value="# Raw Draft\n"),
                patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
                patch("tools.release.cli.render_ai_payload_json", return_value='{"schema_version":"x"}\n'),
                patch("tools.release.cli.build_semantic_ai_payload", return_value={"schema_version": "semantic-x"}),
                patch(
                    "tools.release.cli.render_semantic_ai_payload_json",
                    return_value='{"schema_version":"semantic-x"}\n',
                ),
                patch(
                    "tools.release.cli.parse_release_ai_settings",
                    return_value=type(
                        "_ReleaseSettings",
                        (),
                        {
                            "mode": "auto",
                            "openai_profile": "openai-balanced",
                            "openai_api_key_present": False,
                            "local_enabled": False,
                            "local_profile": None,
                            "local_base_url_override": None,
                            "local_api_key": None,
                        },
                    )(),
                ),
                patch(
                    "tools.release.cli.resolve_release_changelog",
                    return_value=type(
                        "_Selection",
                        (),
                        {"path": "manual", "content": "# Manual Changelog\n", "local_base_url_overrode_profile": False},
                    )(),
                ),
                redirect_stdout(out),
            ):
                rc = cli.cmd_changelog_release(args)

            self.assertEqual(rc, 0)
            self.assertEqual(target.read_text(encoding="utf-8"), "# Manual Changelog\n")
            self.assertEqual(raw_target.read_text(encoding="utf-8"), "# Raw Draft\n")
            self.assertEqual(payload_target.read_text(encoding="utf-8"), '{"schema_version":"x"}\n')
            self.assertEqual(
                semantic_payload_target.read_text(encoding="utf-8"),
                '{"schema_version":"semantic-x"}\n',
            )
            rendered = out.getvalue()
            self.assertIn("Wrote changelog raw evidence to", rendered)
            self.assertIn("Wrote changelog payload evidence to", rendered)
            self.assertIn("Wrote changelog semantic payload evidence to", rendered)
            self.assertIn("Wrote release changelog to", rendered)
            self.assertIn("Debian changelog entry update skipped", rendered)

    def test_release_command_writes_release_notes_artifact_when_selection_includes_it(self) -> None:
        with TemporaryDirectory() as temp_dir:
            target = Path(temp_dir) / "release.md"
            raw_target = Path(temp_dir) / "raw.md"
            payload_target = Path(temp_dir) / "payload.json"
            semantic_payload_target = Path(temp_dir) / "semantic-payload.json"
            release_notes_target = Path(temp_dir) / "release-notes.md"
            args = self._args(
                output=str(target),
                raw_output=str(raw_target),
                payload_output=str(payload_target),
                semantic_payload_output=str(semantic_payload_target),
                release_notes_output=str(release_notes_target),
            )
            out = StringIO()

            with (
                patch("tools.release.cli.build_changelog_context", return_value=object()),
                patch("tools.release.cli.render_release_changelog", return_value="# Raw Draft\n"),
                patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
                patch("tools.release.cli.render_ai_payload_json", return_value='{"schema_version":"x"}\n'),
                patch("tools.release.cli.build_semantic_ai_payload", return_value={"schema_version": "semantic-x"}),
                patch(
                    "tools.release.cli.render_semantic_ai_payload_json",
                    return_value='{"schema_version":"semantic-x"}\n',
                ),
                patch(
                    "tools.release.cli.parse_release_ai_settings",
                    return_value=type(
                        "_ReleaseSettings",
                        (),
                        {
                            "mode": "auto",
                            "openai_profile": "openai-balanced",
                            "openai_api_key_present": True,
                            "local_enabled": False,
                            "local_profile": None,
                            "local_base_url_override": None,
                            "local_api_key": None,
                        },
                    )(),
                ),
                patch(
                    "tools.release.cli.resolve_release_changelog",
                    return_value=type(
                        "_Selection",
                        (),
                        {
                            "path": "openai",
                            "content": "# Final Changelog\n",
                            "release_notes_content": "# Public Notes\n",
                            "local_base_url_overrode_profile": False,
                        },
                    )(),
                ),
                patch("tools.release.cli.refresh_debian_changelog_entry") as refresh,
                redirect_stdout(out),
            ):
                rc = cli.cmd_changelog_release(args)

            self.assertEqual(rc, 0)
            self.assertEqual(release_notes_target.read_text(encoding="utf-8"), "# Public Notes\n")
            refresh.assert_called_once()
            self.assertIn("Wrote release notes artifact to", out.getvalue())

    def test_release_command_refreshes_debian_changelog_for_generated_selection(self) -> None:
        with TemporaryDirectory() as temp_dir:
            target = Path(temp_dir) / "release.md"
            raw_target = Path(temp_dir) / "raw.md"
            payload_target = Path(temp_dir) / "payload.json"
            semantic_payload_target = Path(temp_dir) / "semantic-payload.json"
            args = self._args(
                output=str(target),
                raw_output=str(raw_target),
                payload_output=str(payload_target),
                semantic_payload_output=str(semantic_payload_target),
                debian_distribution="stable",
                debian_urgency="high",
            )
            out = StringIO()

            with (
                patch("tools.release.cli.build_changelog_context", return_value=object()),
                patch("tools.release.cli.render_release_changelog", return_value="# Raw Draft\n"),
                patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
                patch("tools.release.cli.render_ai_payload_json", return_value='{"schema_version":"x"}\n'),
                patch("tools.release.cli.build_semantic_ai_payload", return_value={"schema_version": "semantic-x"}),
                patch(
                    "tools.release.cli.render_semantic_ai_payload_json",
                    return_value='{"schema_version":"semantic-x"}\n',
                ),
                patch(
                    "tools.release.cli.parse_release_ai_settings",
                    return_value=type(
                        "_ReleaseSettings",
                        (),
                        {
                            "mode": "auto",
                            "openai_profile": "openai-balanced",
                            "openai_api_key_present": True,
                            "local_enabled": False,
                            "local_profile": None,
                            "local_base_url_override": None,
                            "local_api_key": None,
                        },
                    )(),
                ),
                patch(
                    "tools.release.cli.resolve_release_changelog",
                    return_value=type(
                        "_Selection",
                        (),
                        {"path": "openai", "content": "# Release\n- change one\n", "local_base_url_overrode_profile": False},
                    )(),
                ),
                patch(
                    "tools.release.cli.refresh_debian_changelog_entry",
                    return_value=type(
                        "_DebianUpdate",
                        (),
                        {
                            "path": Path(temp_dir) / "debian" / "changelog",
                            "package": "vaulthalla",
                            "full_version": "1.2.3-1",
                            "distribution": "stable",
                            "urgency": "high",
                            "maintainer": "Test User <test@example.com>",
                            "timestamp": "Tue, 21 Apr 2026 20:00:00 +0000",
                        },
                    )(),
                ) as refresh_entry,
                redirect_stdout(out),
            ):
                rc = cli.cmd_changelog_release(args)

            self.assertEqual(rc, 0)
            self.assertEqual(
                semantic_payload_target.read_text(encoding="utf-8"),
                '{"schema_version":"semantic-x"}\n',
            )
            refresh_entry.assert_called_once()
            self.assertEqual(refresh_entry.call_args.kwargs["release_markdown"], "# Release\n- change one\n")
            self.assertEqual(refresh_entry.call_args.kwargs["distribution"], "stable")
            self.assertEqual(refresh_entry.call_args.kwargs["urgency"], "high")


class CliChangelogAIReleaseTests(unittest.TestCase):
    def _args(self) -> argparse.Namespace:
        return argparse.Namespace(
            repo_root=".",
            since_tag=None,
            draft_output=".changelog_scratch/changelog.draft.md",
            output=".changelog_scratch/changelog.release.md",
            raw_output=".changelog_scratch/changelog.raw.md",
            payload_output=".changelog_scratch/changelog.payload.json",
            semantic_payload_output=".changelog_scratch/changelog.semantic_payload.json",
            manual_changelog_path="debian/changelog",
            debian_distribution=None,
            debian_urgency=None,
            save_json=None,
            ai_profile="openai-balanced",
            model=None,
            provider="openai",
            base_url=None,
            use_triage=False,
            save_triage_json=None,
            polish=False,
            save_polish_json=None,
            release_notes_output=".changelog_scratch/release_notes.md",
        )

    def test_ai_release_runs_ai_draft_then_forced_cached_release(self) -> None:
        args = self._args()
        out = StringIO()
        observed_modes: list[str | None] = []

        with (
            patch("tools.release.cli.cmd_changelog_ai_draft", return_value=0) as run_ai_draft,
            patch(
                "tools.release.cli.cmd_changelog_release",
                side_effect=lambda _release_args: observed_modes.append(os.environ.get("RELEASE_AI_MODE")) or 0,
            ) as run_release,
            patch.dict("os.environ", {"RELEASE_AI_MODE": "auto"}, clear=False),
            redirect_stdout(out),
        ):
            rc = cli.cmd_changelog_ai_release(args)
            self.assertEqual(os.environ.get("RELEASE_AI_MODE"), "auto")

        self.assertEqual(rc, 0)
        run_ai_draft.assert_called_once()
        run_release.assert_called_once()
        self.assertEqual(observed_modes, ["disabled"])
        expected_cached_path = str((Path(".").resolve() / ".changelog_scratch/changelog.draft.md").resolve())
        self.assertEqual(run_ai_draft.call_args.args[0].output, expected_cached_path)
        self.assertEqual(run_release.call_args.args[0].cached_draft_path, expected_cached_path)
        self.assertEqual(
            run_release.call_args.args[0].semantic_payload_output,
            ".changelog_scratch/changelog.semantic_payload.json",
        )

    def test_ai_release_skips_release_stage_when_draft_returns_nonzero(self) -> None:
        args = self._args()
        with (
            patch("tools.release.cli.cmd_changelog_ai_draft", return_value=2) as run_ai_draft,
            patch("tools.release.cli.cmd_changelog_release", return_value=0) as run_release,
        ):
            rc = cli.cmd_changelog_ai_release(args)

        self.assertEqual(rc, 2)
        run_ai_draft.assert_called_once()
        run_release.assert_not_called()

    def test_ai_release_refreshes_debian_from_fresh_draft_under_repo_root(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir).resolve()
            changelog_path = repo_root / "debian" / "changelog"
            changelog_path.parent.mkdir(parents=True, exist_ok=True)
            changelog_path.write_text(
                (
                    "vaulthalla (1.2.3-1) unstable; urgency=medium\n\n"
                    "  - existing line\n\n"
                    " -- Test User <test@example.com>  Sun, 19 Apr 2026 00:00:00 +0000\n"
                ),
                encoding="utf-8",
            )
            (repo_root / "VERSION").write_text("1.2.3\n", encoding="utf-8")

            stale_cached = cli.render_cached_draft_markdown(version="1.2.3", content="# Old\n- stale body\n")
            stale_cached_path = repo_root / ".changelog_scratch" / "changelog.draft.md"
            stale_cached_path.parent.mkdir(parents=True, exist_ok=True)
            stale_cached_path.write_text(stale_cached, encoding="utf-8")

            fresh_cached = cli.render_cached_draft_markdown(version="1.2.3", content="# Fresh\n- fresh body\n")

            def _fake_ai_draft(draft_args: argparse.Namespace) -> int:
                cli.write_output(fresh_cached, draft_args.output)
                return 0

            args = argparse.Namespace(
                repo_root=str(repo_root),
                since_tag=None,
                draft_output=".changelog_scratch/changelog.draft.md",
                output=str(repo_root / ".changelog_scratch" / "changelog.release.md"),
                raw_output=str(repo_root / ".changelog_scratch" / "changelog.raw.md"),
                payload_output=str(repo_root / ".changelog_scratch" / "changelog.payload.json"),
                semantic_payload_output=str(
                    repo_root / ".changelog_scratch" / "changelog.semantic_payload.json"
                ),
                manual_changelog_path="debian/changelog",
                debian_distribution=None,
                debian_urgency=None,
                save_json=None,
                ai_profile="openai-balanced",
                model=None,
                provider="openai",
                base_url=None,
                use_triage=False,
                save_triage_json=None,
                polish=False,
                save_polish_json=None,
                release_notes_output=str(repo_root / ".changelog_scratch" / "release_notes.md"),
            )

            with (
                patch("tools.release.cli.cmd_changelog_ai_draft", side_effect=_fake_ai_draft),
                patch("tools.release.cli.build_changelog_context", return_value=object()),
                patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
                patch("tools.release.cli.render_release_changelog", return_value="# Raw Draft\n"),
                patch("tools.release.cli.render_ai_payload_json", return_value='{"schema_version":"x"}\n'),
                patch("tools.release.cli.build_semantic_ai_payload", return_value={"schema_version": "semantic-x"}),
                patch(
                    "tools.release.cli.render_semantic_ai_payload_json",
                    return_value='{"schema_version":"semantic-x"}\n',
                ),
            ):
                rc = cli.cmd_changelog_ai_release(args)

            self.assertEqual(rc, 0)
            rendered = changelog_path.read_text(encoding="utf-8")
            self.assertIn("  - fresh body", rendered)
            self.assertNotIn("  - stale body", rendered)


class CliChangelogAICheckTests(unittest.TestCase):
    def _args(
        self,
        *,
        ai_profile: str | None = None,
        provider: str = "openai-compatible",
        model: str = "Qwen3.5-122B",
        base_url: str | None = "http://localhost:8888/v1",
        repo_root: str = ".",
    ) -> argparse.Namespace:
        return argparse.Namespace(
            ai_profile=ai_profile,
            provider=provider,
            model=model,
            base_url=base_url,
            repo_root=repo_root,
        )

    def test_ai_check_prints_success_summary(self) -> None:
        out = StringIO()
        args = self._args()
        result_obj = ProviderPreflightResult(
            provider_kind="openai-compatible",
            model="Qwen3.5-122B",
            base_url="http://localhost:8888/v1",
            discovered_models=("Qwen3.5-122B", "Gemma-4-31B"),
            model_found=True,
        )
        with (
            patch("tools.release.cli.build_ai_provider_config_from_args") as build_config,
            patch("tools.release.cli.build_ai_provider_from_config", return_value=object()) as build_provider,
            patch("tools.release.cli.run_provider_preflight", return_value=result_obj) as run_preflight,
            redirect_stdout(out),
        ):
            rc = cli.cmd_changelog_ai_check(args)

        self.assertEqual(rc, 0)
        build_config.assert_called_once_with(args)
        build_provider.assert_called_once()
        run_preflight.assert_called_once()
        rendered = out.getvalue()
        self.assertIn("AI provider check", rendered)
        self.assertIn("openai-compatible", rendered)
        self.assertIn("Qwen3.5-122B", rendered)
        self.assertIn("Gemma-4-31B", rendered)

    def test_main_ai_check_surfaces_preflight_errors(self) -> None:
        err = StringIO()
        with (
            patch("tools.release.cli.build_ai_provider_from_config", return_value=object()),
            patch(
                "tools.release.cli.run_provider_preflight",
                side_effect=ValueError("Could not reach OpenAI-compatible endpoint"),
            ),
            patch("sys.stderr", new=err),
        ):
            rc = cli.main(
                [
                    "changelog",
                    "ai-check",
                    "--provider",
                    "openai-compatible",
                    "--base-url",
                    "http://localhost:8888/v1",
                    "--model",
                    "Qwen3.5-122B",
                ]
            )

        self.assertEqual(rc, 1)
        self.assertIn("ERROR: Could not reach OpenAI-compatible endpoint", err.getvalue())


class CliChangelogAIDraftTests(unittest.TestCase):
    def _args(
        self,
        *,
        repo_root: str = ".",
        since_tag: str | None = None,
        output: str | None = None,
        save_json: str | None = None,
        model: str | None = DEFAULT_AI_DRAFT_MODEL,
        provider: str | None = "openai",
        base_url: str | None = None,
        ai_profile: str | None = None,
        use_triage: bool = False,
        save_triage_json: str | None = None,
        polish: bool = False,
        save_polish_json: str | None = None,
        release_notes_output: str = ".changelog_scratch/release_notes.md",
    ) -> argparse.Namespace:
        return argparse.Namespace(
            repo_root=repo_root,
            since_tag=since_tag,
            output=output,
            save_json=save_json,
            model=model,
            provider=provider,
            base_url=base_url,
            ai_profile=ai_profile,
            use_triage=use_triage,
            save_triage_json=save_triage_json,
            polish=polish,
            save_polish_json=save_polish_json,
            release_notes_output=release_notes_output,
        )

    def test_ai_draft_to_stdout(self) -> None:
        args = self._args(since_tag="v0.1.0")
        out = StringIO()
        provider_obj = object()

        with (
            patch("tools.release.cli.build_changelog_context", return_value=object()) as build_context,
            patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}) as build_payload,
            patch("tools.release.cli.build_ai_provider_from_args", return_value=provider_obj) as build_provider,
            patch("tools.release.cli.generate_draft_from_payload", return_value=object()) as generate_draft,
            patch("tools.release.cli.render_draft_markdown", return_value="# AI Draft\n") as render_markdown,
            patch("tools.release.cli.render_draft_result_json") as render_json,
            patch("tools.release.cli.run_triage_stage") as run_triage,
            patch("tools.release.cli.render_triage_result_json") as render_triage_json,
            patch("tools.release.cli.run_polish_stage") as run_polish,
            patch("tools.release.cli.render_polish_result_json") as render_polish_json,
            patch("tools.release.cli.render_polish_markdown") as render_polish_markdown,
            patch("tools.release.cli.run_provider_preflight") as run_preflight,
            redirect_stdout(out),
        ):
            result = cli.cmd_changelog_ai_draft(args)

        self.assertEqual(result, 0)
        self.assertEqual(out.getvalue(), "# AI Draft\n")
        self.assertEqual(build_context.call_args.args[1], "v0.1.0")
        build_payload.assert_called_once()
        build_provider.assert_called_once_with(args, repo_root=Path(".").resolve(), stage="draft")
        generate_draft.assert_called_once_with(
            {"schema_version": "x"},
            provider=provider_obj,
            source_kind="payload",
            provider_kind="openai",
            reasoning_effort=None,
            structured_mode=None,
            temperature=0.2,
            max_output_tokens_policy=AIDynamicRatioTokenBudget(mode="dynamic_ratio", ratio=0.35, min=800, max=4000),
        )
        render_markdown.assert_called_once()
        render_json.assert_not_called()
        run_triage.assert_not_called()
        render_triage_json.assert_not_called()
        run_polish.assert_not_called()
        render_polish_json.assert_not_called()
        render_polish_markdown.assert_not_called()
        run_preflight.assert_not_called()

    def test_ai_draft_output_and_json_files(self) -> None:
        with TemporaryDirectory() as temp_dir:
            markdown_target = Path(temp_dir) / "ai-draft.md"
            json_target = Path(temp_dir) / "ai-draft.json"
            triage_obj = object()
            args = self._args(
                output=str(markdown_target),
                save_json=str(json_target),
                model="gpt-x-mini",
                use_triage=True,
            )
            out = StringIO()
            provider_obj = object()

            with (
                patch("tools.release.cli.build_changelog_context", return_value=object()),
                patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
                patch("tools.release.cli.build_semantic_ai_payload", return_value={"schema_version": "semantic-x"}),
                patch("tools.release.cli.build_ai_provider_from_args", return_value=provider_obj) as build_provider,
                patch("tools.release.cli.run_triage_stage", return_value=triage_obj) as run_triage,
                patch("tools.release.cli.build_triage_ir_payload", return_value={"schema_version": "triage-x"}) as build_triage,
                patch("tools.release.cli.generate_draft_from_payload", return_value=object()) as generate_draft,
                patch("tools.release.cli.render_draft_markdown", return_value="# AI Draft\n"),
                patch("tools.release.cli.render_draft_result_json", return_value='{"title":"x"}\n'),
                patch("tools.release.cli.render_triage_result_json", return_value='{"schema_version":"triage"}\n'),
                patch("tools.release.cli.run_polish_stage") as run_polish,
                patch("tools.release.cli.render_polish_result_json") as render_polish_json,
                patch("tools.release.cli.render_polish_markdown") as render_polish_markdown,
                redirect_stdout(out),
            ):
                result = cli.cmd_changelog_ai_draft(args)

            self.assertEqual(result, 0)
            rendered_markdown = markdown_target.read_text(encoding="utf-8")
            self.assertIn("<!-- vaulthalla-release-version:", rendered_markdown)
            self.assertIn("# AI Draft", rendered_markdown)
            self.assertEqual(json_target.read_text(encoding="utf-8"), '{"title":"x"}\n')
            run_triage.assert_called_once_with(
                {"schema_version": "semantic-x"},
                provider=provider_obj,
                provider_kind="openai",
                reasoning_effort=None,
                structured_mode=None,
                temperature=0.0,
                max_output_tokens_policy=300,
                input_mode="raw_semantic",
            )
            build_provider.assert_has_calls(
                [
                    call(args, repo_root=Path(".").resolve(), stage="draft"),
                    call(args, repo_root=Path(".").resolve(), stage="triage"),
                ]
            )
            build_triage.assert_called_once_with(triage_obj)
            generate_draft.assert_called_once_with(
                {"schema_version": "triage-x"},
                provider=provider_obj,
                source_kind="triage",
                provider_kind="openai",
                reasoning_effort=None,
                structured_mode=None,
                temperature=0.2,
                max_output_tokens_policy=AIDynamicRatioTokenBudget(mode="dynamic_ratio", ratio=0.35, min=800, max=4000),
            )
            self.assertIn("Wrote AI changelog draft to", out.getvalue())
            self.assertIn("Wrote AI draft JSON to", out.getvalue())
            run_polish.assert_not_called()
            render_polish_json.assert_not_called()
            render_polish_markdown.assert_not_called()

    def test_ai_draft_can_save_triage_json(self) -> None:
        with TemporaryDirectory() as temp_dir:
            triage_target = Path(temp_dir) / "triage.json"
            args = self._args(use_triage=True, save_triage_json=str(triage_target))
            out = StringIO()
            provider_obj = object()

            with (
                patch("tools.release.cli.build_changelog_context", return_value=object()),
                patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
                patch("tools.release.cli.build_semantic_ai_payload", return_value={"schema_version": "semantic-x"}),
                patch("tools.release.cli.build_ai_provider_from_args", return_value=provider_obj) as build_provider,
                patch("tools.release.cli.run_triage_stage", return_value=object()),
                patch("tools.release.cli.build_triage_ir_payload", return_value={"schema_version": "triage-x"}),
                patch("tools.release.cli.generate_draft_from_payload", return_value=object()),
                patch("tools.release.cli.render_draft_markdown", return_value="# AI Draft\n"),
                patch("tools.release.cli.render_triage_result_json", return_value='{"schema_version":"triage"}\n'),
                patch("tools.release.cli.render_draft_result_json"),
                patch("tools.release.cli.run_polish_stage"),
                patch("tools.release.cli.render_polish_result_json"),
                patch("tools.release.cli.render_polish_markdown"),
                redirect_stdout(out),
            ):
                result = cli.cmd_changelog_ai_draft(args)

            self.assertEqual(result, 0)
            self.assertEqual(triage_target.read_text(encoding="utf-8"), '{"schema_version":"triage"}\n')
            build_provider.assert_has_calls(
                [
                    call(args, repo_root=Path(".").resolve(), stage="draft"),
                    call(args, repo_root=Path(".").resolve(), stage="triage"),
                ]
            )
            self.assertIn("Wrote AI triage JSON to", out.getvalue())

    def test_ai_draft_emergency_triage_routes_triage_to_synthesized_mode_and_writes_artifact(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            (repo_root / "VERSION").write_text("1.2.3\n", encoding="utf-8")
            (repo_root / "ai.yml").write_text(
                """
profiles:
  openai-balanced:
    provider: openai
    stages:
      emergency_triage:
        model: gpt-5-nano
      triage:
        model: gpt-5-nano
      draft:
        model: gpt-5-mini
""",
                encoding="utf-8",
            )
            args = self._args(
                repo_root=str(repo_root),
                ai_profile="openai-balanced",
                provider=None,
                model=None,
                use_triage=False,
            )
            out = StringIO()
            provider_obj = object()
            emergency_obj = object()

            with (
                patch("tools.release.cli.build_changelog_context", return_value=object()),
                patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
                patch(
                    "tools.release.cli.build_semantic_ai_payload",
                    return_value={
                        "schema_version": "vaulthalla.release.semantic_payload.v1",
                        "version": "1.2.3",
                        "categories": [],
                    },
                ),
                patch("tools.release.cli.build_ai_provider_from_args", return_value=provider_obj),
                patch("tools.release.cli.run_emergency_triage_stage", return_value=emergency_obj) as run_emergency,
                patch(
                    "tools.release.cli.render_emergency_triage_result_json",
                    return_value='{"schema_version":"vaulthalla.release.ai_emergency_triage.v1","version":"1.2.3","items":[]}\n',
                ),
                patch(
                    "tools.release.cli.build_triage_input_from_emergency_result",
                    return_value={"schema_version": "vaulthalla.release.triage_input.synthesized.v1", "categories": []},
                ) as build_triage_input,
                patch("tools.release.cli.run_triage_stage", return_value=object()) as run_triage,
                patch("tools.release.cli.build_triage_ir_payload", return_value={"schema_version": "triage-x"}),
                patch("tools.release.cli.generate_draft_from_payload", return_value=object()),
                patch("tools.release.cli.render_draft_markdown", return_value="# AI Draft\n"),
                patch("tools.release.cli.render_draft_result_json"),
                patch("tools.release.cli.render_triage_result_json"),
                patch("tools.release.cli.run_polish_stage"),
                patch("tools.release.cli.render_polish_result_json"),
                patch("tools.release.cli.render_polish_markdown"),
                redirect_stdout(out),
            ):
                result = cli.cmd_changelog_ai_draft(args)

            self.assertEqual(result, 0)
            run_emergency.assert_called_once_with(
                {
                    "schema_version": "vaulthalla.release.semantic_payload.v1",
                    "version": "1.2.3",
                    "categories": [],
                },
                provider=provider_obj,
                provider_kind="openai",
                reasoning_effort=None,
                structured_mode=None,
                temperature=0.0,
                max_output_tokens_policy=AIDynamicRatioTokenBudget(mode="dynamic_ratio", ratio=0.55, min=1200, max=12000),
            )
            build_triage_input.assert_called_once_with(
                {
                    "schema_version": "vaulthalla.release.semantic_payload.v1",
                    "version": "1.2.3",
                    "categories": [],
                },
                emergency_obj,
            )
            run_triage.assert_called_once_with(
                {"schema_version": "vaulthalla.release.triage_input.synthesized.v1", "categories": []},
                provider=provider_obj,
                provider_kind="openai",
                reasoning_effort=None,
                structured_mode=None,
                temperature=0.0,
                max_output_tokens_policy=300,
                input_mode="synthesized_semantic",
            )
            emergency_artifact = repo_root / ".changelog_scratch" / "emergency_triage.json"
            self.assertTrue(emergency_artifact.is_file())
            self.assertIn("vaulthalla.release.ai_emergency_triage.v1", emergency_artifact.read_text(encoding="utf-8"))

    def test_ai_draft_polish_stage_output_and_json(self) -> None:
        with TemporaryDirectory() as temp_dir:
            markdown_target = Path(temp_dir) / "ai-polished.md"
            json_target = Path(temp_dir) / "ai-polished.json"
            polish_target = Path(temp_dir) / "polish.json"
            draft_obj = object()
            polish_obj = object()
            args = self._args(
                output=str(markdown_target),
                save_json=str(json_target),
                polish=True,
                save_polish_json=str(polish_target),
            )
            out = StringIO()
            provider_obj = object()

            with (
                patch("tools.release.cli.build_changelog_context", return_value=object()),
                patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
                patch("tools.release.cli.build_ai_provider_from_args", return_value=provider_obj) as build_provider,
                patch("tools.release.cli.generate_draft_from_payload", return_value=draft_obj) as generate_draft,
                patch("tools.release.cli.run_polish_stage", return_value=polish_obj) as run_polish,
                patch("tools.release.cli.render_draft_markdown", return_value="# AI Draft\n") as render_draft_markdown,
                patch("tools.release.cli.render_polish_markdown", return_value="# AI Polished\n") as render_polish_markdown,
                patch("tools.release.cli.render_draft_result_json", return_value='{"title":"draft"}\n') as render_draft_json,
                patch("tools.release.cli.render_polish_result_json", return_value='{"schema_version":"polish"}\n') as render_polish_json,
                patch("tools.release.cli.run_triage_stage") as run_triage,
                patch("tools.release.cli.render_triage_result_json") as render_triage_json,
                redirect_stdout(out),
            ):
                result = cli.cmd_changelog_ai_draft(args)

            self.assertEqual(result, 0)
            rendered_markdown = markdown_target.read_text(encoding="utf-8")
            self.assertIn("<!-- vaulthalla-release-version:", rendered_markdown)
            self.assertIn("# AI Polished", rendered_markdown)
            self.assertEqual(json_target.read_text(encoding="utf-8"), '{"schema_version":"polish"}\n')
            self.assertEqual(polish_target.read_text(encoding="utf-8"), '{"schema_version":"polish"}\n')
            generate_draft.assert_called_once_with(
                {"schema_version": "x"},
                provider=provider_obj,
                source_kind="payload",
                provider_kind="openai",
                reasoning_effort=None,
                structured_mode=None,
                temperature=0.2,
                max_output_tokens_policy=AIDynamicRatioTokenBudget(mode="dynamic_ratio", ratio=0.35, min=800, max=4000),
            )
            run_polish.assert_called_once_with(
                draft_obj,
                provider=provider_obj,
                provider_kind="openai",
                reasoning_effort=None,
                structured_mode=None,
                temperature=0.0,
                max_output_tokens_policy=800,
            )
            build_provider.assert_has_calls(
                [
                    call(args, repo_root=Path(".").resolve(), stage="draft"),
                    call(args, repo_root=Path(".").resolve(), stage="polish"),
                ]
            )
            render_polish_markdown.assert_called_once_with(polish_obj)
            render_polish_json.assert_called()
            render_draft_markdown.assert_not_called()
            render_draft_json.assert_not_called()
            run_triage.assert_not_called()
            render_triage_json.assert_not_called()
            self.assertIn("Wrote AI polish JSON to", out.getvalue())

    def test_ai_draft_release_notes_stage_writes_artifact_when_profile_enables_it(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            (repo_root / "VERSION").write_text("1.2.3\n", encoding="utf-8")
            (repo_root / "ai.yml").write_text(
                """
profiles:
  openai-balanced:
    provider: openai
    stages:
      draft:
        model: gpt-5-mini
      release_notes:
        model: gpt-5.4
        reasoning_effort: high
""",
                encoding="utf-8",
            )
            notes_target = repo_root / ".changelog_scratch" / "release_notes.md"
            args = self._args(
                repo_root=str(repo_root),
                ai_profile="openai-balanced",
                provider=None,
                model=None,
                release_notes_output=str(notes_target),
            )
            out = StringIO()
            provider_obj = object()
            release_notes_obj = SimpleNamespace(markdown="# Public Notes\n- polished item\n")

            with (
                patch("tools.release.cli.build_changelog_context", return_value=object()),
                patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
                patch("tools.release.cli.build_ai_provider_from_args", return_value=provider_obj) as build_provider,
                patch("tools.release.cli.generate_draft_from_payload", return_value=object()),
                patch("tools.release.cli.render_draft_markdown", return_value="# Final Changelog\n- item\n"),
                patch("tools.release.cli.run_release_notes_stage", return_value=release_notes_obj) as run_release_notes,
                patch("tools.release.cli.run_triage_stage"),
                patch("tools.release.cli.run_polish_stage"),
                patch("tools.release.cli.render_polish_markdown"),
                redirect_stdout(out),
            ):
                result = cli.cmd_changelog_ai_draft(args)

            self.assertEqual(result, 0)
            run_release_notes.assert_called_once_with(
                "# Final Changelog\n- item\n",
                provider=provider_obj,
                provider_kind="openai",
                reasoning_effort="high",
                structured_mode=None,
                temperature=0.0,
                max_output_tokens_policy=1200,
            )
            self.assertIn(
                call(args, repo_root=repo_root.resolve(), stage="release_notes"),
                build_provider.call_args_list,
            )
            self.assertTrue(notes_target.is_file())
            self.assertEqual(notes_target.read_text(encoding="utf-8"), "# Public Notes\n- polished item\n")
            self.assertIn("Wrote AI release notes to", out.getvalue())

    def test_ai_draft_release_notes_uses_draft_input_even_when_polish_enabled(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            (repo_root / "VERSION").write_text("1.2.3\n", encoding="utf-8")
            (repo_root / "ai.yml").write_text(
                """
profiles:
  openai-balanced:
    provider: openai
    stages:
      draft:
        model: gpt-5-mini
      polish:
        model: gpt-5.4
      release_notes:
        model: gpt-5.4
""",
                encoding="utf-8",
            )
            output_target = repo_root / ".changelog_scratch" / "changelog.release.md"
            notes_target = repo_root / ".changelog_scratch" / "release_notes.md"
            args = self._args(
                repo_root=str(repo_root),
                ai_profile="openai-balanced",
                provider=None,
                model=None,
                output=str(output_target),
                release_notes_output=str(notes_target),
            )
            provider_obj = object()
            draft_obj = object()
            polish_obj = object()
            release_notes_obj = SimpleNamespace(markdown="# Public Notes\n- from draft\n")

            with (
                patch("tools.release.cli.build_changelog_context", return_value=object()),
                patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
                patch("tools.release.cli.build_ai_provider_from_args", return_value=provider_obj),
                patch("tools.release.cli.generate_draft_from_payload", return_value=draft_obj),
                patch("tools.release.cli.render_draft_markdown", return_value="# Draft Output\n- d\n"),
                patch("tools.release.cli.run_polish_stage", return_value=polish_obj) as run_polish,
                patch("tools.release.cli.render_polish_markdown", return_value="# Polished Output\n- p\n"),
                patch("tools.release.cli.run_release_notes_stage", return_value=release_notes_obj) as run_release_notes,
                patch("tools.release.cli.run_triage_stage"),
            ):
                result = cli.cmd_changelog_ai_draft(args)

            self.assertEqual(result, 0)
            run_polish.assert_called_once_with(
                draft_obj,
                provider=provider_obj,
                provider_kind="openai",
                reasoning_effort=None,
                structured_mode=None,
                temperature=0.0,
                max_output_tokens_policy=800,
            )
            run_release_notes.assert_called_once_with(
                "# Draft Output\n- d\n",
                provider=provider_obj,
                provider_kind="openai",
                reasoning_effort=None,
                structured_mode=None,
                temperature=0.0,
                max_output_tokens_policy=1200,
            )
            rendered_changelog = output_target.read_text(encoding="utf-8")
            self.assertIn("# Polished Output", rendered_changelog)
            self.assertEqual(notes_target.read_text(encoding="utf-8"), "# Public Notes\n- from draft\n")

    def test_ai_draft_runs_polish_and_release_notes_concurrently(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            (repo_root / "VERSION").write_text("1.2.3\n", encoding="utf-8")
            (repo_root / "ai.yml").write_text(
                """
profiles:
  openai-balanced:
    provider: openai
    stages:
      draft:
        model: gpt-5-mini
      polish:
        model: gpt-5.4
      release_notes:
        model: gpt-5.4
""",
                encoding="utf-8",
            )
            args = self._args(
                repo_root=str(repo_root),
                ai_profile="openai-balanced",
                provider=None,
                model=None,
            )
            provider_obj = object()
            draft_obj = object()
            polish_obj = object()
            release_notes_obj = SimpleNamespace(markdown="# Public Notes\n- concurrent\n")
            polish_started = threading.Event()
            release_notes_started = threading.Event()

            def _run_polish(*_args, **_kwargs):
                polish_started.set()
                self.assertTrue(
                    release_notes_started.wait(timeout=1.0),
                    "release_notes did not start while polish was running",
                )
                return polish_obj

            def _run_release_notes(*_args, **_kwargs):
                release_notes_started.set()
                self.assertTrue(
                    polish_started.wait(timeout=1.0),
                    "polish did not start while release_notes was running",
                )
                return release_notes_obj

            with (
                patch("tools.release.cli.build_changelog_context", return_value=object()),
                patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
                patch("tools.release.cli.build_ai_provider_from_args", return_value=provider_obj),
                patch("tools.release.cli.generate_draft_from_payload", return_value=draft_obj),
                patch("tools.release.cli.render_draft_markdown", return_value="# Draft Output\n- d\n"),
                patch("tools.release.cli.run_polish_stage", side_effect=_run_polish),
                patch("tools.release.cli.render_polish_markdown", return_value="# Polished Output\n- p\n"),
                patch("tools.release.cli.run_release_notes_stage", side_effect=_run_release_notes),
                patch("tools.release.cli.run_triage_stage"),
            ):
                result = cli.cmd_changelog_ai_draft(args)

            self.assertEqual(result, 0)

    def test_ai_draft_fails_when_release_notes_sibling_stage_fails(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            (repo_root / "VERSION").write_text("1.2.3\n", encoding="utf-8")
            (repo_root / "ai.yml").write_text(
                """
profiles:
  openai-balanced:
    provider: openai
    stages:
      draft:
        model: gpt-5-mini
      polish:
        model: gpt-5.4
      release_notes:
        model: gpt-5.4
""",
                encoding="utf-8",
            )
            args = self._args(
                repo_root=str(repo_root),
                ai_profile="openai-balanced",
                provider=None,
                model=None,
            )
            provider_obj = object()

            with (
                patch("tools.release.cli.build_changelog_context", return_value=object()),
                patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
                patch("tools.release.cli.build_ai_provider_from_args", return_value=provider_obj),
                patch("tools.release.cli.generate_draft_from_payload", return_value=object()),
                patch("tools.release.cli.render_draft_markdown", return_value="# Draft Output\n- d\n"),
                patch("tools.release.cli.run_polish_stage", return_value=object()),
                patch("tools.release.cli.render_polish_markdown", return_value="# Polished Output\n- p\n"),
                patch("tools.release.cli.run_release_notes_stage", side_effect=ValueError("notes parse failure")),
                patch("tools.release.cli.run_triage_stage"),
            ):
                with self.assertRaisesRegex(ValueError, "Release notes stage failed: notes parse failure"):
                    _ = cli.cmd_changelog_ai_draft(args)

    def test_ai_draft_triage_and_polish_pipeline_order(self) -> None:
        triage_obj = object()
        draft_obj = object()
        polish_obj = object()
        args = self._args(use_triage=True, polish=True, model="gpt-x-mini")
        out = StringIO()
        provider_obj = object()

        with (
            patch("tools.release.cli.build_changelog_context", return_value=object()),
            patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
            patch("tools.release.cli.build_semantic_ai_payload", return_value={"schema_version": "semantic-x"}),
            patch("tools.release.cli.build_ai_provider_from_args", return_value=provider_obj) as build_provider,
            patch("tools.release.cli.run_triage_stage", return_value=triage_obj) as run_triage,
            patch("tools.release.cli.build_triage_ir_payload", return_value={"schema_version": "triage-x"}) as build_triage,
            patch("tools.release.cli.generate_draft_from_payload", return_value=draft_obj) as generate_draft,
            patch("tools.release.cli.run_polish_stage", return_value=polish_obj) as run_polish,
            patch("tools.release.cli.render_polish_markdown", return_value="# Polished\n"),
            patch("tools.release.cli.render_polish_result_json"),
            patch("tools.release.cli.render_draft_markdown"),
            patch("tools.release.cli.render_draft_result_json"),
            patch("tools.release.cli.render_triage_result_json"),
            redirect_stdout(out),
        ):
            result = cli.cmd_changelog_ai_draft(args)

        self.assertEqual(result, 0)
        build_provider.assert_has_calls(
            [
                call(args, repo_root=Path(".").resolve(), stage="draft"),
                call(args, repo_root=Path(".").resolve(), stage="triage"),
                call(args, repo_root=Path(".").resolve(), stage="polish"),
            ]
        )
        run_triage.assert_called_once_with(
            {"schema_version": "semantic-x"},
            provider=provider_obj,
            provider_kind="openai",
            reasoning_effort=None,
            structured_mode=None,
            temperature=0.0,
            max_output_tokens_policy=300,
            input_mode="raw_semantic",
        )
        build_triage.assert_called_once_with(triage_obj)
        generate_draft.assert_called_once_with(
            {"schema_version": "triage-x"},
            provider=provider_obj,
            source_kind="triage",
            provider_kind="openai",
            reasoning_effort=None,
            structured_mode=None,
            temperature=0.2,
            max_output_tokens_policy=AIDynamicRatioTokenBudget(mode="dynamic_ratio", ratio=0.35, min=800, max=4000),
        )
        run_polish.assert_called_once_with(
            draft_obj,
            provider=provider_obj,
            provider_kind="openai",
            reasoning_effort=None,
            structured_mode=None,
            temperature=0.0,
            max_output_tokens_policy=800,
        )

    def test_ai_draft_stage_reasoning_and_mode_resolve_from_profile(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            profile_cfg = repo_root / "ai.yml"
            (repo_root / "VERSION").write_text("1.2.3\n", encoding="utf-8")
            profile_cfg.write_text(
                """
profiles:
  openai-balanced:
    provider: openai
    stages:
      triage:
        model: gpt-5-nano
        reasoning_effort: low
        structured_mode: json_object
      draft:
        model: gpt-5-mini
        reasoning_effort: medium
        structured_mode: strict_json_schema
      polish:
        model: gpt-5.4
        reasoning_effort: high
        structured_mode: prompt_json
""",
                encoding="utf-8",
            )
            args = self._args(
                repo_root=str(repo_root),
                ai_profile="openai-balanced",
                provider=None,
                model=None,
                use_triage=True,
                polish=True,
            )
            provider_obj = object()
            draft_obj = object()

            with (
                patch("tools.release.cli.build_changelog_context", return_value=object()),
                patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
                patch("tools.release.cli.build_semantic_ai_payload", return_value={"schema_version": "semantic-x"}),
                patch("tools.release.cli.build_ai_provider_from_args", return_value=provider_obj),
                patch("tools.release.cli.run_triage_stage", return_value=object()) as run_triage,
                patch("tools.release.cli.build_triage_ir_payload", return_value={"schema_version": "triage-x"}),
                patch("tools.release.cli.generate_draft_from_payload", return_value=draft_obj) as generate_draft,
                patch("tools.release.cli.run_polish_stage", return_value=object()) as run_polish,
                patch("tools.release.cli.render_polish_markdown", return_value="# Polished\n"),
                patch("tools.release.cli.render_polish_result_json"),
                patch("tools.release.cli.render_draft_markdown"),
                patch("tools.release.cli.render_draft_result_json"),
                patch("tools.release.cli.render_triage_result_json"),
            ):
                result = cli.cmd_changelog_ai_draft(args)

            self.assertEqual(result, 0)
            run_triage.assert_called_once_with(
                {"schema_version": "semantic-x"},
                provider=provider_obj,
                provider_kind="openai",
                reasoning_effort="low",
                structured_mode="json_object",
                temperature=0.0,
                max_output_tokens_policy=300,
                input_mode="raw_semantic",
            )
            generate_draft.assert_called_once_with(
                {"schema_version": "triage-x"},
                provider=provider_obj,
                source_kind="triage",
                provider_kind="openai",
                reasoning_effort="medium",
                structured_mode="strict_json_schema",
                temperature=0.2,
                max_output_tokens_policy=AIDynamicRatioTokenBudget(mode="dynamic_ratio", ratio=0.35, min=800, max=4000),
            )
            run_polish.assert_called_once_with(
                draft_obj,
                provider=provider_obj,
                provider_kind="openai",
                reasoning_effort="high",
                structured_mode="prompt_json",
                temperature=0.0,
                max_output_tokens_policy=800,
            )

    def test_ai_draft_profile_defined_stages_run_without_cli_flags(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            (repo_root / "VERSION").write_text("1.2.3\n", encoding="utf-8")
            (repo_root / "ai.yml").write_text(
                """
profiles:
  local:
    provider: openai
    stages:
      polish:
        model: gpt-polish
      draft:
        model: gpt-draft
      triage:
        model: gpt-triage
""",
                encoding="utf-8",
            )
            args = self._args(
                repo_root=str(repo_root),
                ai_profile="local",
                provider=None,
                model=None,
                use_triage=False,
                polish=False,
            )
            provider_obj = object()
            call_order: list[str] = []

            with (
                patch("tools.release.cli.build_changelog_context", return_value=object()),
                patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
                patch("tools.release.cli.build_semantic_ai_payload", return_value={"schema_version": "semantic-x"}),
                patch("tools.release.cli.build_ai_provider_from_args", return_value=provider_obj),
                patch("tools.release.cli.build_triage_ir_payload", return_value={"schema_version": "triage-x"}),
                patch(
                    "tools.release.cli.run_triage_stage",
                    side_effect=lambda *a, **k: call_order.append("triage") or object(),
                ),
                patch(
                    "tools.release.cli.generate_draft_from_payload",
                    side_effect=lambda *a, **k: call_order.append("draft") or object(),
                ),
                patch(
                    "tools.release.cli.run_polish_stage",
                    side_effect=lambda *a, **k: call_order.append("polish") or object(),
                ),
                patch("tools.release.cli.render_polish_markdown", return_value="# Polished\n"),
                patch("tools.release.cli.render_polish_result_json"),
                patch("tools.release.cli.render_draft_markdown"),
                patch("tools.release.cli.render_draft_result_json"),
                patch("tools.release.cli.render_triage_result_json"),
            ):
                result = cli.cmd_changelog_ai_draft(args)

            self.assertEqual(result, 0)
            self.assertEqual(call_order, ["triage", "draft", "polish"])

    def test_main_fails_when_triage_requested_and_invalid(self) -> None:
        err = StringIO()
        with (
            patch("tools.release.cli.build_changelog_context", return_value=object()),
            patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
            patch("tools.release.cli.build_semantic_ai_payload", return_value={"schema_version": "semantic-x"}),
            patch("tools.release.cli.build_ai_provider_from_args", return_value=object()),
            patch(
                "tools.release.cli.run_triage_stage",
                side_effect=ValueError("AI triage response must include non-empty `categories` list."),
            ),
            patch("sys.stderr", new=err),
        ):
            rc = cli.main(["changelog", "ai-draft", "--use-triage"])

        self.assertEqual(rc, 1)
        self.assertIn("ERROR: Triage stage failed:", err.getvalue())
        self.assertIn("categories", err.getvalue())

    def test_main_fails_with_stage_specific_triage_missing_schema_version_message(self) -> None:
        err = StringIO()
        with (
            patch("tools.release.cli.build_changelog_context", return_value=object()),
            patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
            patch("tools.release.cli.build_semantic_ai_payload", return_value={"schema_version": "semantic-x"}),
            patch("tools.release.cli.build_ai_provider_from_args", return_value=object()),
            patch(
                "tools.release.cli.run_triage_stage",
                side_effect=ValueError("`schema_version` must be a non-empty string."),
            ),
            patch("sys.stderr", new=err),
        ):
            rc = cli.main(["changelog", "ai-draft", "--use-triage"])

        self.assertEqual(rc, 1)
        self.assertIn("ERROR: Triage stage failed: missing required field `schema_version`.", err.getvalue())

    def test_main_fails_when_polish_requested_and_invalid(self) -> None:
        err = StringIO()
        with (
            patch("tools.release.cli.build_changelog_context", return_value=object()),
            patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
            patch("tools.release.cli.build_ai_provider_from_args", return_value=object()),
            patch("tools.release.cli.generate_draft_from_payload", return_value=object()),
            patch(
                "tools.release.cli.run_polish_stage",
                side_effect=ValueError("AI polish response must include non-empty `sections` list."),
            ),
            patch("sys.stderr", new=err),
        ):
            rc = cli.main(["changelog", "ai-draft", "--polish"])

        self.assertEqual(rc, 1)
        self.assertIn("ERROR: Polish stage failed:", err.getvalue())
        self.assertIn("sections", err.getvalue())

    def test_main_fails_with_stage_specific_draft_missing_title_message(self) -> None:
        err = StringIO()
        with (
            patch("tools.release.cli.build_changelog_context", return_value=object()),
            patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
            patch("tools.release.cli.build_ai_provider_from_args", return_value=object()),
            patch(
                "tools.release.cli.generate_draft_from_payload",
                side_effect=ValueError("`title` must be a non-empty string."),
            ),
            patch("sys.stderr", new=err),
        ):
            rc = cli.main(["changelog", "ai-draft"])

        self.assertEqual(rc, 1)
        self.assertIn("ERROR: Draft stage failed: missing required field `title`.", err.getvalue())

    def test_main_fails_when_save_triage_json_used_without_triage(self) -> None:
        err = StringIO()
        with patch("sys.stderr", new=err):
            rc = cli.main(["changelog", "ai-draft", "--save-triage-json", "/tmp/triage.json"])

        self.assertEqual(rc, 1)
        self.assertIn("ERROR: --save-triage-json requires triage stage to run.", err.getvalue())

    def test_main_fails_when_save_polish_json_used_without_polish(self) -> None:
        err = StringIO()
        with patch("sys.stderr", new=err):
            rc = cli.main(["changelog", "ai-draft", "--save-polish-json", "/tmp/polish.json"])

        self.assertEqual(rc, 1)
        self.assertIn("ERROR: --save-polish-json requires polish stage to run.", err.getvalue())

    def test_main_reports_missing_api_key_error_cleanly(self) -> None:
        err = StringIO()
        with (
            patch("tools.release.cli.build_changelog_context", return_value=object()),
            patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
            patch(
                "tools.release.cli.build_ai_provider_from_args",
                side_effect=ValueError("OPENAI_API_KEY is not set."),
            ),
            patch("tools.release.cli.run_triage_stage"),
            patch("tools.release.cli.run_polish_stage"),
            patch("sys.stderr", new=err),
        ):
            rc = cli.main(["changelog", "ai-draft"])

        self.assertEqual(rc, 1)
        self.assertIn("ERROR: OPENAI_API_KEY is not set.", err.getvalue())

    def test_main_openai_rejects_base_url_flag(self) -> None:
        err = StringIO()
        with (
            patch("tools.release.cli.build_changelog_context", return_value=object()),
            patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
            patch("sys.stderr", new=err),
        ):
            rc = cli.main(
                [
                    "changelog",
                    "ai-draft",
                    "--provider",
                    "openai",
                    "--base-url",
                    "http://localhost:8888/v1",
                ]
            )

        self.assertEqual(rc, 1)
        self.assertIn("ERROR: `--base-url` is only valid when `--provider openai-compatible` is used.", err.getvalue())

    def test_main_openai_compatible_allows_no_auth_provider_build(self) -> None:
        err = StringIO()
        out = StringIO()
        with (
            patch("tools.release.cli.build_changelog_context", return_value=object()),
            patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
            patch("tools.release.cli.build_ai_provider_from_args", return_value=object()),
            patch("tools.release.cli.run_provider_preflight"),
            patch("tools.release.cli.generate_draft_from_payload", return_value=object()),
            patch("tools.release.cli.render_draft_markdown", return_value="# Draft\n"),
            patch("sys.stderr", new=err),
            redirect_stdout(out),
        ):
            rc = cli.main(
                [
                    "changelog",
                    "ai-draft",
                    "--provider",
                    "openai-compatible",
                    "--base-url",
                    "http://localhost:8888/v1",
                    "--model",
                    "Qwen3.5-122B",
                ]
            )

        self.assertEqual(rc, 0)
        self.assertEqual(err.getvalue(), "")
        self.assertEqual(out.getvalue(), "# Draft\n")

    def test_ai_draft_openai_compatible_runs_preflight(self) -> None:
        args = self._args(provider="openai-compatible", base_url="http://localhost:8888/v1", model="Qwen3.5-122B")
        out = StringIO()
        provider_obj = object()

        with (
            patch("tools.release.cli.build_changelog_context", return_value=object()),
            patch("tools.release.cli.build_ai_payload", return_value={"schema_version": "x"}),
            patch("tools.release.cli.build_ai_provider_from_args", return_value=provider_obj),
            patch("tools.release.cli.run_provider_preflight") as run_preflight,
            patch("tools.release.cli.generate_draft_from_payload", return_value=object()),
            patch("tools.release.cli.render_draft_markdown", return_value="# Draft\n"),
            redirect_stdout(out),
        ):
            rc = cli.cmd_changelog_ai_draft(args)

        self.assertEqual(rc, 0)
        run_preflight.assert_called_once()
        call = run_preflight.call_args
        self.assertEqual(call.kwargs["provider"], provider_obj)
        self.assertTrue(call.kwargs["require_model"])
        self.assertEqual(call.args[0].kind, "openai-compatible")
        self.assertEqual(call.args[0].base_url, "http://localhost:8888/v1")
        self.assertEqual(call.args[0].model, "Qwen3.5-122B")


class CliChangelogAICompareTests(unittest.TestCase):
    def _write_repo_basics(self, repo_root: Path) -> str:
        original_debian = (
            "vaulthalla (0.34.0-1) unstable; urgency=medium\n\n"
            "  - existing line\n\n"
            " -- Test User <test@example.com>  Sun, 19 Apr 2026 00:00:00 +0000\n"
        )
        (repo_root / "VERSION").write_text("0.34.0\n", encoding="utf-8")
        debian_path = repo_root / "debian" / "changelog"
        debian_path.parent.mkdir(parents=True, exist_ok=True)
        debian_path.write_text(original_debian, encoding="utf-8")
        return original_debian

    def test_ai_compare_writes_combined_markdown_artifact(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            original_debian = self._write_repo_basics(repo_root)
            args = argparse.Namespace(
                repo_root=str(repo_root),
                since_tag=None,
                ai_profiles="openai-cheap,openai-balanced",
                output_name="0.34.0-openai-comparison.md",
            )

            def _fake_draft(draft_args: argparse.Namespace) -> int:
                cached = cli.render_cached_draft_markdown(
                    version="0.34.0",
                    content=f"# {draft_args.ai_profile}\n- {draft_args.ai_profile} summary",
                )
                Path(draft_args.output).write_text(cached, encoding="utf-8")
                return 0

            def _fake_pipeline(_draft_args: argparse.Namespace, *, repo_root: Path | None = None):
                return SimpleNamespace(
                    provider="openai",
                    base_url=None,
                    profile_slug=None,
                    enabled_stages=("triage", "draft", "polish"),
                    stages={
                        "emergency_triage": SimpleNamespace(
                            model="gpt-5-nano",
                            reasoning_effort="low",
                            structured_mode="strict_json_schema",
                            max_output_tokens=SimpleNamespace(mode="dynamic_ratio", ratio=0.55, min=1200, max=12000),
                        ),
                        "triage": SimpleNamespace(
                            model="gpt-5-nano",
                            reasoning_effort="low",
                            structured_mode=None,
                            max_output_tokens=300,
                        ),
                        "draft": SimpleNamespace(
                            model="gpt-5-mini",
                            reasoning_effort="medium",
                            structured_mode="strict_json_schema",
                            max_output_tokens=SimpleNamespace(mode="dynamic_ratio", ratio=0.35, min=800, max=4000),
                        ),
                        "polish": SimpleNamespace(
                            model="gpt-5.4",
                            reasoning_effort="medium",
                            structured_mode=None,
                            max_output_tokens=800,
                        ),
                        "release_notes": SimpleNamespace(
                            model="gpt-5.4",
                            reasoning_effort="high",
                            structured_mode=None,
                            max_output_tokens=1200,
                        ),
                    },
                    is_stage_enabled=lambda stage: stage in {"triage", "draft", "polish"},
                )

            def _fake_refresh(**kwargs):
                changelog_path = Path(kwargs["changelog_path"])
                heading = kwargs["release_markdown"].splitlines()[0].strip()
                changelog_path.write_text(f"effective debian from {heading}\n", encoding="utf-8")
                return SimpleNamespace(path=changelog_path)

            with (
                patch("tools.release.cli.cmd_changelog_ai_draft", side_effect=_fake_draft) as run_draft,
                patch("tools.release.cli.build_ai_pipeline_config_from_args", side_effect=_fake_pipeline),
                patch("tools.release.cli.refresh_debian_changelog_entry", side_effect=_fake_refresh) as refresh_entry,
            ):
                rc = cli.cmd_changelog_ai_compare(args)

            self.assertEqual(rc, 0)
            artifact = (
                repo_root / ".changelog_scratch" / "comparisons" / "0.34.0-openai-comparison.md"
            )
            self.assertTrue(artifact.is_file())
            rendered = artifact.read_text(encoding="utf-8")
            self.assertIn("# AI Changelog Comparison — 0.34.0", rendered)
            self.assertIn("## Profile: openai-cheap", rendered)
            self.assertIn("## Profile: openai-balanced", rendered)
            self.assertIn("### changelog.release.md", rendered)
            self.assertIn("### Effective profile config", rendered)
            self.assertIn("```yaml", rendered)
            self.assertIn("# openai-cheap", rendered)
            self.assertIn("# openai-balanced", rendered)
            self.assertIn("### debian/changelog", rendered)
            self.assertIn("### release_notes.md", rendered)
            self.assertIn("_not generated_", rendered)
            self.assertIn("effective debian from # openai-cheap", rendered)
            self.assertIn("effective debian from # openai-balanced", rendered)
            self.assertIn("provider: openai", rendered)
            self.assertIn("profile_slug: <none>", rendered)
            self.assertIn("enabled_stages:", rendered)
            self.assertIn("default_max_output_tokens:", rendered)
            self.assertIn("mode: dynamic_ratio", rendered)
            self.assertIn("ratio: 0.35", rendered)
            self.assertIn("stages:", rendered)
            self.assertIn("reasoning_effort: medium", rendered)
            self.assertIn("structured_mode: strict_json_schema", rendered)
            self.assertEqual(run_draft.call_count, 2)
            self.assertEqual(refresh_entry.call_count, 2)
            for call_ in refresh_entry.call_args_list:
                self.assertNotEqual(
                    Path(call_.kwargs["changelog_path"]).resolve(),
                    (repo_root / "debian" / "changelog").resolve(),
                )
            self.assertEqual((repo_root / "debian" / "changelog").read_text(encoding="utf-8"), original_debian)

    def test_ai_compare_rejects_invalid_output_name(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            (repo_root / "VERSION").write_text("0.34.0\n", encoding="utf-8")
            args = argparse.Namespace(
                repo_root=str(repo_root),
                since_tag=None,
                ai_profiles="openai-cheap",
                output_name="../bad.md",
            )
            with self.assertRaisesRegex(ValueError, "--output-name must be a file name"):
                _ = cli.cmd_changelog_ai_compare(args)

    def test_ai_compare_real_triage_path_handles_semantic_tuple_projection(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _ = self._write_repo_basics(repo_root)
            (repo_root / "ai.yml").write_text(
                """
profiles:
  openai-cheap:
    provider: openai
    stages:
      triage:
        model: gpt-5-nano
      draft:
        model: gpt-5-nano
      polish:
        model: gpt-5-mini
      release_notes:
        model: gpt-5-mini
""",
                encoding="utf-8",
            )
            args = argparse.Namespace(
                repo_root=str(repo_root),
                since_tag=None,
                ai_profiles="openai-cheap",
                output_name="0.34.0-openai-comparison.md",
            )

            context = ReleaseContext(
                version="0.34.0",
                previous_tag="v0.33.0",
                head_sha="abc123",
                commit_count=1,
                categories={
                    "tools": CategoryContext(
                        name="tools",
                        commit_count=1,
                        insertions=10,
                        deletions=1,
                        commits=[
                            CommitInfo(
                                sha="abcd1234",
                                subject="Refine ai-compare profile projection",
                                body="",
                                files=["tools/release/cli.py"],
                                insertions=10,
                                deletions=1,
                                categories=["tools"],
                            )
                        ],
                        files=[
                            FileChange(
                                path="tools/release/cli.py",
                                category="tools",
                                subscopes=("release", "cli"),
                                insertions=10,
                                deletions=1,
                                commit_count=1,
                                score=10.0,
                                flags=("release-tooling",),
                            )
                        ],
                        snippets=[
                            DiffSnippet(
                                path="tools/release/cli.py",
                                category="tools",
                                subscopes=("release", "cli"),
                                score=8.0,
                                reason="Selected from high-scoring tools file; flags: release-tooling",
                                patch="@@ -1,2 +1,2 @@\n+compare wiring update",
                                flags=("release-tooling",),
                            )
                        ],
                        detected_themes=["release-automation"],
                    )
                },
                cross_cutting_notes=[],
            )

            class _StageAwareProvider:
                def __init__(self) -> None:
                    self.triage_projection_categories = 0

                def generate_structured_json(self, **kwargs):
                    stage = kwargs["stage"]
                    if stage == "triage":
                        marker = "Semantic payload (compact projection):\n"
                        projection = json.loads(kwargs["user_prompt"].split(marker, 1)[1])
                        self.triage_projection_categories = len(projection.get("categories", []))
                        if self.triage_projection_categories <= 0:
                            raise AssertionError("triage projection categories unexpectedly empty")
                        return {
                            "schema_version": "vaulthalla.release.ai_triage.v2",
                            "version": "0.34.0",
                            "categories": [
                                {
                                    "name": "tools",
                                    "signal_strength": "strong",
                                    "priority_rank": 1,
                                    "theme": "Release tooling updates",
                                    "grounded_claims": ["AI compare output and contract flow were updated."],
                                }
                            ],
                        }
                    if stage == "draft":
                        return {
                            "title": "Release 0.34.0",
                            "summary": "Release tooling updates.",
                            "sections": [
                                {
                                    "category": "tools",
                                    "overview": "Release tooling changed.",
                                    "bullets": ["AI compare output and contract flow were updated."],
                                }
                            ],
                            "notes": [],
                        }
                    if stage == "polish":
                        return {
                            "schema_version": "vaulthalla.release.ai_polish.v1",
                            "title": "Release 0.34.0",
                            "summary": "Release tooling updates.",
                            "sections": [
                                {
                                    "category": "tools",
                                    "overview": "Release tooling changed.",
                                    "bullets": ["AI compare output and contract flow were updated."],
                                }
                            ],
                            "notes": [],
                        }
                    if stage == "release_notes":
                        return {
                            "schema_version": "vaulthalla.release.ai_release_notes.v1",
                            "markdown": "# Public Notes\n- AI compare output and contract flow were updated.\n",
                        }
                    raise AssertionError(f"Unexpected stage: {stage}")

            provider = _StageAwareProvider()

            with (
                patch("tools.release.cli.build_changelog_context", return_value=context),
                patch("tools.release.cli.build_ai_provider_from_args", return_value=provider),
            ):
                rc = cli.cmd_changelog_ai_compare(args)

            self.assertEqual(rc, 0)
            self.assertGreater(provider.triage_projection_categories, 0)
            artifact = repo_root / ".changelog_scratch" / "comparisons" / "0.34.0-openai-comparison.md"
            self.assertTrue(artifact.is_file())
            self.assertIn("## Profile: openai-cheap", artifact.read_text(encoding="utf-8"))

    def test_ai_compare_emergency_triage_batches_and_preserves_batch_identity(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _ = self._write_repo_basics(repo_root)
            (repo_root / "ai.yml").write_text(
                """
profiles:
  openai-cheap:
    provider: openai
    stages:
      emergency_triage:
        model: gpt-5-nano
      triage:
        model: gpt-5-nano
      draft:
        model: gpt-5-nano
""",
                encoding="utf-8",
            )
            args = argparse.Namespace(
                repo_root=str(repo_root),
                since_tag=None,
                ai_profiles="openai-cheap",
                output_name="0.34.0-openai-comparison.md",
            )

            snippets = [
                DiffSnippet(
                    path="tools/release/cli.py",
                    category="tools",
                    subscopes=("release", "cli"),
                    score=8.0 - (index * 0.1),
                    reason=f"Selected snippet {index}",
                    patch=f"@@ -{index + 1},2 +{index + 1},2 @@\n+batch update {index}",
                    flags=("release-tooling",),
                    region_kind="function",
                    region_label=f"ctx_{index}",
                    hunk_count=1,
                    changed_lines=6,
                    meaningful_lines=6,
                )
                for index in range(5)
            ]
            context = ReleaseContext(
                version="0.34.0",
                previous_tag="v0.33.0",
                head_sha="abc123",
                commit_count=1,
                categories={
                    "tools": CategoryContext(
                        name="tools",
                        commit_count=1,
                        insertions=10,
                        deletions=1,
                        commits=[
                            CommitInfo(
                                sha="abcd1234",
                                subject="Refine emergency triage batching",
                                body="",
                                files=["tools/release/cli.py"],
                                insertions=10,
                                deletions=1,
                                categories=["tools"],
                            )
                        ],
                        files=[
                            FileChange(
                                path="tools/release/cli.py",
                                category="tools",
                                subscopes=("release", "cli"),
                                insertions=10,
                                deletions=1,
                                commit_count=1,
                                score=10.0,
                                flags=("release-tooling",),
                            )
                        ],
                        snippets=snippets,
                        detected_themes=["release-automation"],
                    )
                },
                cross_cutting_notes=[],
            )

            class _StageAwareProvider:
                def __init__(self) -> None:
                    self.emergency_batch_sizes: list[int] = []
                    self.emergency_schema_id_enums: list[list[str]] = []

                def generate_structured_json(self, **kwargs):
                    stage = kwargs["stage"]
                    if stage == "emergency_triage":
                        marker = "Emergency triage input payload:\n"
                        projection = json.loads(kwargs["user_prompt"].split(marker, 1)[1])
                        items = projection.get("items", [])
                        self.emergency_batch_sizes.append(len(items))
                        schema = kwargs["json_schema"]
                        enum_ids = schema["properties"]["items"]["items"]["properties"]["id"]["enum"]
                        self.emergency_schema_id_enums.append(list(enum_ids))
                        return {
                            "schema_version": "vaulthalla.release.ai_emergency_triage.v1",
                            "version": "0.34.0",
                            "items": [
                                {
                                    "id": item["id"],
                                    "category": item["category"],
                                    "change_kind": "implementation-change",
                                    "change_summary": f"Summarized {item['id']}.",
                                    "confidence": "medium",
                                    "insufficient_context_reason": None,
                                    "evidence_refs": [],
                                }
                                for item in items
                            ],
                        }
                    if stage == "triage":
                        marker = "Synthesized semantic payload (compact projection):\n"
                        projection = json.loads(kwargs["user_prompt"].split(marker, 1)[1])
                        categories = projection.get("categories", [])
                        if not categories:
                            raise AssertionError("expected synthesized triage categories")
                        units = categories[0].get("synthesized_units", [])
                        if len(units) != 5:
                            raise AssertionError(f"expected 5 synthesized units, got {len(units)}")
                        return {
                            "schema_version": "vaulthalla.release.ai_triage.v2",
                            "version": "0.34.0",
                            "categories": [
                                {
                                    "name": "tools",
                                    "signal_strength": "strong",
                                    "priority_rank": 1,
                                    "theme": "Release tooling updates",
                                    "grounded_claims": ["Emergency triage batched unit synthesis preserved identity."],
                                }
                            ],
                        }
                    if stage == "draft":
                        return {
                            "title": "Release 0.34.0",
                            "summary": "Release tooling updates.",
                            "sections": [
                                {
                                    "category": "tools",
                                    "overview": "Release tooling changed.",
                                    "bullets": ["Emergency triage batched unit synthesis preserved identity."],
                                }
                            ],
                            "notes": [],
                        }
                    raise AssertionError(f"Unexpected stage: {stage}")

            provider = _StageAwareProvider()

            with (
                patch("tools.release.cli.build_changelog_context", return_value=context),
                patch("tools.release.cli.build_ai_provider_from_args", return_value=provider),
                patch("tools.release.changelog.ai.stages.emergency_triage._EMERGENCY_TRIAGE_BATCH_SIZE", 2),
            ):
                rc = cli.cmd_changelog_ai_compare(args)

            self.assertEqual(rc, 0)
            self.assertEqual(provider.emergency_batch_sizes, [2, 2, 1])
            self.assertEqual(
                provider.emergency_schema_id_enums,
                [["tools:1", "tools:2"], ["tools:3", "tools:4"], ["tools:5"]],
            )
            artifact = repo_root / ".changelog_scratch" / "comparisons" / "0.34.0-openai-comparison.md"
            self.assertTrue(artifact.is_file())
            self.assertIn("## Profile: openai-cheap", artifact.read_text(encoding="utf-8"))

    def test_ai_compare_emergency_triage_defaults_to_bounded_scope(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _ = self._write_repo_basics(repo_root)
            (repo_root / "ai.yml").write_text(
                """
profiles:
  openai-cheap:
    provider: openai
    stages:
      emergency_triage:
        model: gpt-5-nano
      triage:
        model: gpt-5-nano
      draft:
        model: gpt-5-nano
""",
                encoding="utf-8",
            )
            args = argparse.Namespace(
                repo_root=str(repo_root),
                since_tag=None,
                ai_profiles="openai-cheap",
                output_name="0.34.0-openai-comparison.md",
            )

            snippets = [
                DiffSnippet(
                    path="tools/release/cli.py",
                    category="tools",
                    subscopes=("release", "cli"),
                    score=8.0 - (index * 0.1),
                    reason=f"Selected snippet {index}",
                    patch=f"@@ -{index + 1},2 +{index + 1},2 @@\n+batch update {index}",
                    flags=("release-tooling",),
                    region_kind="function",
                    region_label=f"ctx_{index}",
                    hunk_count=1,
                    changed_lines=6,
                    meaningful_lines=6,
                )
                for index in range(3)
            ]
            context = ReleaseContext(
                version="0.34.0",
                previous_tag="v0.33.0",
                head_sha="abc123",
                commit_count=1,
                categories={
                    "tools": CategoryContext(
                        name="tools",
                        commit_count=1,
                        insertions=10,
                        deletions=1,
                        commits=[
                            CommitInfo(
                                sha="abcd1234",
                                subject="Refine emergency triage request scope",
                                body="",
                                files=["tools/release/cli.py"],
                                insertions=10,
                                deletions=1,
                                categories=["tools"],
                            )
                        ],
                        files=[
                            FileChange(
                                path="tools/release/cli.py",
                                category="tools",
                                subscopes=("release", "cli"),
                                insertions=10,
                                deletions=1,
                                commit_count=1,
                                score=10.0,
                                flags=("release-tooling",),
                            )
                        ],
                        snippets=snippets,
                        detected_themes=["release-automation"],
                    )
                },
                cross_cutting_notes=[],
            )

            class _StageAwareProvider:
                def __init__(self) -> None:
                    self.emergency_batch_sizes: list[int] = []

                def generate_structured_json(self, **kwargs):
                    stage = kwargs["stage"]
                    if stage == "emergency_triage":
                        marker = "Emergency triage input payload:\n"
                        projection = json.loads(kwargs["user_prompt"].split(marker, 1)[1])
                        items = projection.get("items", [])
                        self.emergency_batch_sizes.append(len(items))
                        return {
                            "schema_version": "vaulthalla.release.ai_emergency_triage.v1",
                            "version": "0.34.0",
                            "items": [
                                {
                                    "id": item["id"],
                                    "category": item["category"],
                                    "change_kind": "implementation-change",
                                    "change_summary": f"Summarized {item['id']}.",
                                    "confidence": "medium",
                                    "insufficient_context_reason": None,
                                    "evidence_refs": [],
                                }
                                for item in items
                            ],
                        }
                    if stage == "triage":
                        return {
                            "schema_version": "vaulthalla.release.ai_triage.v2",
                            "version": "0.34.0",
                            "categories": [
                                {
                                    "name": "tools",
                                    "signal_strength": "strong",
                                    "priority_rank": 1,
                                    "theme": "Release tooling updates",
                                    "grounded_claims": ["Per-item emergency triage preserved unit identity."],
                                }
                            ],
                        }
                    if stage == "draft":
                        return {
                            "title": "Release 0.34.0",
                            "summary": "Release tooling updates.",
                            "sections": [
                                {
                                    "category": "tools",
                                    "overview": "Release tooling changed.",
                                    "bullets": ["Per-item emergency triage preserved unit identity."],
                                }
                            ],
                            "notes": [],
                        }
                    raise AssertionError(f"Unexpected stage: {stage}")

            provider = _StageAwareProvider()
            with (
                patch("tools.release.cli.build_changelog_context", return_value=context),
                patch("tools.release.cli.build_ai_provider_from_args", return_value=provider),
            ):
                rc = cli.cmd_changelog_ai_compare(args)

            self.assertEqual(rc, 0)
            self.assertEqual(provider.emergency_batch_sizes, [3])


class CliChangelogContextHelperTests(unittest.TestCase):
    def test_build_changelog_context_reads_version_and_passes_since_tag(self) -> None:
        repo_root = Path("/tmp/repo")
        context_obj = object()

        with (
            patch("tools.release.cli.read_version_file", return_value=Version(1, 2, 3)) as read_version,
            patch("tools.release.cli.build_release_context", return_value=context_obj) as build_context,
        ):
            context = cli.build_changelog_context(repo_root, "v0.9.0")

        self.assertIs(context, context_obj)
        read_version.assert_called_once_with(repo_root / "VERSION")
        build_context.assert_called_once_with(version="1.2.3", repo_root=repo_root, previous_tag="v0.9.0")


if __name__ == "__main__":
    unittest.main()
