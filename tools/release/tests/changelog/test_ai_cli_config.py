from __future__ import annotations

import argparse
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from tools.release import cli


def _write_profile(repo_root: Path, content: str) -> None:
    cfg = repo_root / "ai.yml"
    cfg.write_text(content, encoding="utf-8")


class AICLIConfigResolutionTests(unittest.TestCase):
    def _args(
        self,
        *,
        repo_root: str,
        ai_profile: str | None = None,
        provider: str | None = None,
        model: str | None = None,
        base_url: str | None = None,
    ) -> argparse.Namespace:
        return argparse.Namespace(
            repo_root=repo_root,
            ai_profile=ai_profile,
            provider=provider,
            model=model,
            base_url=base_url,
        )

    def test_profile_values_apply_when_cli_overrides_absent(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
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
            )
            args = self._args(repo_root=str(repo_root), ai_profile="openai-balanced")

            triage_cfg = cli.build_ai_provider_config_from_args(args, stage="triage")
            draft_cfg = cli.build_ai_provider_config_from_args(args, stage="draft")
            polish_cfg = cli.build_ai_provider_config_from_args(args, stage="polish")
            pipeline = cli.build_ai_pipeline_config_from_args(args)

        self.assertEqual(triage_cfg.model, "gpt-5-nano")
        self.assertEqual(draft_cfg.model, "gpt-5-mini")
        self.assertEqual(polish_cfg.model, "gpt-5.4")
        self.assertEqual(draft_cfg.kind, "openai")
        self.assertEqual(pipeline.stages["triage"].reasoning_effort, "low")
        self.assertEqual(pipeline.stages["triage"].structured_mode, "json_object")
        self.assertEqual(pipeline.stages["draft"].reasoning_effort, "medium")
        self.assertEqual(pipeline.stages["draft"].structured_mode, "strict_json_schema")
        self.assertEqual(pipeline.stages["polish"].reasoning_effort, "high")
        self.assertEqual(pipeline.stages["polish"].structured_mode, "prompt_json")

    def test_profile_stage_tuning_resolves_with_defaults(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  tuned:
    provider: openai
    default_temperature:
      draft: 0.4
    default_max_output_tokens:
      draft: 1200
    stages:
      draft:
        model: gpt-draft
        temperature: 0.1
""",
            )
            args = self._args(repo_root=str(repo_root), ai_profile="tuned")
            pipeline = cli.build_ai_pipeline_config_from_args(args)

        self.assertEqual(pipeline.stages["draft"].temperature, 0.1)
        self.assertEqual(pipeline.stages["draft"].max_output_tokens, 1200)
        self.assertEqual(pipeline.stages["triage"].temperature, 0.0)

    def test_cli_model_override_beats_profile_stage_models(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  local-gemma:
    provider: openai-compatible
    base_url: http://127.0.0.1:8888/v1
    stages:
      triage:
        model: Gemma-4-31B
      draft:
        model: Gemma-4-31B
      polish:
        model: Gemma-4-31B
""",
            )
            args = self._args(
                repo_root=str(repo_root),
                ai_profile="local-gemma",
                model="Qwen3.5-122B",
            )

            triage_cfg = cli.build_ai_provider_config_from_args(args, stage="triage")
            draft_cfg = cli.build_ai_provider_config_from_args(args, stage="draft")
            polish_cfg = cli.build_ai_provider_config_from_args(args, stage="polish")

        self.assertEqual(triage_cfg.model, "Qwen3.5-122B")
        self.assertEqual(draft_cfg.model, "Qwen3.5-122B")
        self.assertEqual(polish_cfg.model, "Qwen3.5-122B")
        self.assertEqual(draft_cfg.base_url, "http://127.0.0.1:8888/v1")

    def test_global_fallback_model_applies_when_stage_missing(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  custom:
    provider: openai
    model: gpt-fallback
    stages:
      draft:
        model: gpt-draft
""",
            )
            args = self._args(repo_root=str(repo_root), ai_profile="custom")

            triage_cfg = cli.build_ai_provider_config_from_args(args, stage="triage")
            draft_cfg = cli.build_ai_provider_config_from_args(args, stage="draft")
            polish_cfg = cli.build_ai_provider_config_from_args(args, stage="polish")

        self.assertEqual(triage_cfg.model, "gpt-fallback")
        self.assertEqual(draft_cfg.model, "gpt-draft")
        self.assertEqual(polish_cfg.model, "gpt-fallback")

    def test_backward_compatible_non_profile_cli_usage_still_works(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            args = self._args(
                repo_root=str(repo_root),
                provider="openai-compatible",
                base_url="http://localhost:8888/v1",
                model="Qwen3.5-122B",
            )

            cfg = cli.build_ai_provider_config_from_args(args, stage="draft")

        self.assertEqual(cfg.kind, "openai-compatible")
        self.assertEqual(cfg.base_url, "http://localhost:8888/v1")
        self.assertEqual(cfg.model, "Qwen3.5-122B")

    def test_profile_requested_but_missing_file_surfaces_clear_error(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            args = self._args(repo_root=str(repo_root), ai_profile="local-gemma")
            with self.assertRaises(ValueError) as ctx:
                _ = cli.build_ai_provider_config_from_args(args, stage="draft")

        self.assertIn("config file is missing", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()
