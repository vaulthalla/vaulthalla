from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from tools.release.changelog.ai.config import (
    AIPipelineCLIOverrides,
    resolve_ai_pipeline_config,
)


def _write_profile(repo_root: Path, content: str) -> None:
    cfg = repo_root / ".vaulthalla" / "ai.yml"
    cfg.parent.mkdir(parents=True, exist_ok=True)
    cfg.write_text(content, encoding="utf-8")


class AIControlPlaneConfigTests(unittest.TestCase):
    def test_valid_profile_loads(self) -> None:
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

            resolved = resolve_ai_pipeline_config(
                repo_root=repo_root,
                profile_slug="local-gemma",
                cli_overrides=AIPipelineCLIOverrides(),
            )

        self.assertEqual(resolved.provider, "openai-compatible")
        self.assertEqual(resolved.base_url, "http://127.0.0.1:8888/v1")
        self.assertEqual(resolved.stage_model("triage"), "Gemma-4-31B")
        self.assertEqual(resolved.stage_model("draft"), "Gemma-4-31B")
        self.assertEqual(resolved.stage_model("polish"), "Gemma-4-31B")

    def test_invalid_yaml_returns_clear_error(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(repo_root, "profiles: [broken")

            with self.assertRaises(ValueError) as ctx:
                resolve_ai_pipeline_config(
                    repo_root=repo_root,
                    profile_slug="x",
                    cli_overrides=AIPipelineCLIOverrides(),
                )

        self.assertIn("Invalid AI profile config YAML", str(ctx.exception))

    def test_unknown_profile_slug_returns_clear_error(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  one:
    provider: openai
""",
            )

            with self.assertRaises(ValueError) as ctx:
                resolve_ai_pipeline_config(
                    repo_root=repo_root,
                    profile_slug="missing",
                    cli_overrides=AIPipelineCLIOverrides(),
                )

        self.assertIn("Unknown AI profile `missing`", str(ctx.exception))

    def test_invalid_profile_shape_returns_clear_error(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  bad:
    provider: openai
    stages:
      triage: nope
""",
            )

            with self.assertRaises(ValueError) as ctx:
                resolve_ai_pipeline_config(
                    repo_root=repo_root,
                    profile_slug="bad",
                    cli_overrides=AIPipelineCLIOverrides(),
                )

        self.assertIn("profiles.bad.stages.triage", str(ctx.exception))

    def test_profile_values_apply_without_cli_overrides(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  balanced:
    provider: openai-compatible
    base_url: http://127.0.0.1:9999/v1
    model: shared-model
    reasoning_effort: low
    structured_mode: json_object
    stages:
      draft:
        model: draft-model
        reasoning_effort: high
        structured_mode: prompt_json
""",
            )

            resolved = resolve_ai_pipeline_config(
                repo_root=repo_root,
                profile_slug="balanced",
                cli_overrides=AIPipelineCLIOverrides(),
            )

        self.assertEqual(resolved.provider, "openai-compatible")
        self.assertEqual(resolved.base_url, "http://127.0.0.1:9999/v1")
        self.assertEqual(resolved.stage_model("draft"), "draft-model")
        self.assertEqual(resolved.stage_model("triage"), "shared-model")
        self.assertEqual(resolved.stage_model("polish"), "shared-model")
        self.assertEqual(resolved.stages["triage"].reasoning_effort, "low")
        self.assertEqual(resolved.stages["triage"].structured_mode, "json_object")
        self.assertEqual(resolved.stages["draft"].reasoning_effort, "high")
        self.assertEqual(resolved.stages["draft"].structured_mode, "prompt_json")

    def test_cli_overrides_beat_profile_values(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  local:
    provider: openai-compatible
    base_url: http://127.0.0.1:8888/v1
    stages:
      triage:
        model: triage-a
      draft:
        model: draft-a
      polish:
        model: polish-a
""",
            )

            resolved = resolve_ai_pipeline_config(
                repo_root=repo_root,
                profile_slug="local",
                cli_overrides=AIPipelineCLIOverrides(
                    provider="openai",
                    model="override-all",
                ),
            )

        self.assertEqual(resolved.provider, "openai")
        self.assertEqual(resolved.stage_model("triage"), "override-all")
        self.assertEqual(resolved.stage_model("draft"), "override-all")
        self.assertEqual(resolved.stage_model("polish"), "override-all")

    def test_stage_specific_model_beats_global_fallback(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  custom:
    provider: openai
    model: fallback-x
    stages:
      draft:
        model: draft-x
""",
            )

            resolved = resolve_ai_pipeline_config(
                repo_root=repo_root,
                profile_slug="custom",
                cli_overrides=AIPipelineCLIOverrides(),
            )

        self.assertEqual(resolved.stage_model("triage"), "fallback-x")
        self.assertEqual(resolved.stage_model("draft"), "draft-x")
        self.assertEqual(resolved.stage_model("polish"), "fallback-x")

    def test_provider_base_url_resolution_stable_per_stage(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  local:
    provider: openai-compatible
    base_url: http://127.0.0.1:8888/v1
    model: local-model
""",
            )

            resolved = resolve_ai_pipeline_config(
                repo_root=repo_root,
                profile_slug="local",
                cli_overrides=AIPipelineCLIOverrides(),
            )

        triage_cfg = resolved.provider_config_for_stage("triage")
        draft_cfg = resolved.provider_config_for_stage("draft")
        polish_cfg = resolved.provider_config_for_stage("polish")
        self.assertEqual(triage_cfg.kind, "openai-compatible")
        self.assertEqual(draft_cfg.kind, "openai-compatible")
        self.assertEqual(polish_cfg.kind, "openai-compatible")
        self.assertEqual(triage_cfg.base_url, "http://127.0.0.1:8888/v1")
        self.assertEqual(draft_cfg.base_url, "http://127.0.0.1:8888/v1")
        self.assertEqual(polish_cfg.base_url, "http://127.0.0.1:8888/v1")

    def test_profile_requested_but_config_missing_errors(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            with self.assertRaises(ValueError) as ctx:
                resolve_ai_pipeline_config(
                    repo_root=repo_root,
                    profile_slug="missing",
                    cli_overrides=AIPipelineCLIOverrides(),
                )

        self.assertIn("config file is missing", str(ctx.exception))

    def test_invalid_provider_field_errors(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  bad:
    provider: not-a-provider
""",
            )

            with self.assertRaises(ValueError) as ctx:
                resolve_ai_pipeline_config(
                    repo_root=repo_root,
                    profile_slug="bad",
                    cli_overrides=AIPipelineCLIOverrides(),
                )

        self.assertIn("unsupported provider", str(ctx.exception))

    def test_invalid_reasoning_effort_rejected(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  bad:
    provider: openai
    stages:
      draft:
        model: gpt-test
        reasoning_effort: turbo
""",
            )

            with self.assertRaises(ValueError) as ctx:
                resolve_ai_pipeline_config(
                    repo_root=repo_root,
                    profile_slug="bad",
                    cli_overrides=AIPipelineCLIOverrides(),
                )

        self.assertIn("reasoning_effort", str(ctx.exception))

    def test_invalid_structured_mode_rejected(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  bad:
    provider: openai
    stages:
      draft:
        model: gpt-test
        structured_mode: xml
""",
            )

            with self.assertRaises(ValueError) as ctx:
                resolve_ai_pipeline_config(
                    repo_root=repo_root,
                    profile_slug="bad",
                    cli_overrides=AIPipelineCLIOverrides(),
                )

        self.assertIn("structured_mode", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()
