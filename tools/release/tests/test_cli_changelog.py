from __future__ import annotations

import argparse
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import call, patch

from tools.release import cli
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
            self.assertEqual(markdown_target.read_text(encoding="utf-8"), "# AI Draft\n")
            self.assertEqual(json_target.read_text(encoding="utf-8"), '{"title":"x"}\n')
            run_triage.assert_called_once_with(
                {"schema_version": "x"},
                provider=provider_obj,
                provider_kind="openai",
                reasoning_effort=None,
                structured_mode=None,
                temperature=0.0,
                max_output_tokens_policy=300,
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
            self.assertEqual(markdown_target.read_text(encoding="utf-8"), "# AI Polished\n")
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
            {"schema_version": "x"},
            provider=provider_obj,
            provider_kind="openai",
            reasoning_effort=None,
            structured_mode=None,
            temperature=0.0,
            max_output_tokens_policy=300,
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
                {"schema_version": "x"},
                provider=provider_obj,
                provider_kind="openai",
                reasoning_effort="low",
                structured_mode="json_object",
                temperature=0.0,
                max_output_tokens_policy=300,
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
            patch("tools.release.cli.build_ai_provider_from_args", return_value=object()),
            patch(
                "tools.release.cli.run_triage_stage",
                side_effect=ValueError("AI triage response must include non-empty `categories` list."),
            ),
            patch("sys.stderr", new=err),
        ):
            rc = cli.main(["changelog", "ai-draft", "--use-triage"])

        self.assertEqual(rc, 1)
        self.assertIn("ERROR: AI triage response must include non-empty `categories` list.", err.getvalue())

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
        self.assertIn("ERROR: AI polish response must include non-empty `sections` list.", err.getvalue())

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
