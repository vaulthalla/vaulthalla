from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from tools.release.changelog.ai.config import (
    AIDynamicRatioTokenBudget,
    AIPipelineCLIOverrides,
    compute_max_output_tokens,
    resolve_ai_pipeline_config,
)


def _write_profile(repo_root: Path, content: str) -> None:
    cfg = repo_root / "ai.yml"
    cfg.write_text(content, encoding="utf-8")


class AIControlPlaneConfigTests(unittest.TestCase):
    def test_profile_config_without_schema_version_loads(self) -> None:
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
      draft:
        model: Gemma-4-31B
""",
            )
            resolved = resolve_ai_pipeline_config(
                repo_root=repo_root,
                profile_slug="local-gemma",
                cli_overrides=AIPipelineCLIOverrides(),
            )

        self.assertEqual(resolved.provider, "openai-compatible")

    def test_profile_config_with_valid_schema_version_loads(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
schema_version: vaulthalla.release.ai_profile.v1
profiles:
  local-gemma:
    provider: openai-compatible
    base_url: http://127.0.0.1:8888/v1
    stages:
      draft:
        model: Gemma-4-31B
""",
            )
            resolved = resolve_ai_pipeline_config(
                repo_root=repo_root,
                profile_slug="local-gemma",
                cli_overrides=AIPipelineCLIOverrides(),
            )

        self.assertEqual(resolved.provider, "openai-compatible")

    def test_profile_config_empty_schema_version_fails_clearly(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
schema_version: "   "
profiles:
  local-gemma:
    provider: openai-compatible
    stages:
      draft:
        model: Gemma-4-31B
""",
            )
            with self.assertRaises(ValueError) as ctx:
                resolve_ai_pipeline_config(
                    repo_root=repo_root,
                    profile_slug="local-gemma",
                    cli_overrides=AIPipelineCLIOverrides(),
                )

        self.assertIn("`schema_version` must be a non-empty string.", str(ctx.exception))

    def test_profile_config_unsupported_schema_version_fails_clearly(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
schema_version: vaulthalla.release.ai_profile.v99
profiles:
  local-gemma:
    provider: openai-compatible
    stages:
      draft:
        model: Gemma-4-31B
""",
            )
            with self.assertRaises(ValueError) as ctx:
                resolve_ai_pipeline_config(
                    repo_root=repo_root,
                    profile_slug="local-gemma",
                    cli_overrides=AIPipelineCLIOverrides(),
                )

        self.assertIn("Unsupported AI profile schema_version", str(ctx.exception))

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
      draft:
        model: gpt-test
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

    def test_missing_required_draft_stage_errors(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  bad:
    provider: openai
    stages:
      triage:
        model: gpt-test
""",
            )

            with self.assertRaises(ValueError) as ctx:
                resolve_ai_pipeline_config(
                    repo_root=repo_root,
                    profile_slug="bad",
                    cli_overrides=AIPipelineCLIOverrides(),
                )

        self.assertIn("required stage `draft` is missing", str(ctx.exception))

    def test_empty_stages_mapping_errors(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  bad:
    provider: openai
    stages: {}
""",
            )

            with self.assertRaises(ValueError) as ctx:
                resolve_ai_pipeline_config(
                    repo_root=repo_root,
                    profile_slug="bad",
                    cli_overrides=AIPipelineCLIOverrides(),
                )

        self.assertIn("required stage `draft` is missing", str(ctx.exception))

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

    def test_enabled_stages_use_fixed_contract_order(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  ordered:
    provider: openai
    stages:
      polish:
        model: polish-x
      draft:
        model: draft-x
      triage:
        model: triage-x
""",
            )
            resolved = resolve_ai_pipeline_config(
                repo_root=repo_root,
                profile_slug="ordered",
                cli_overrides=AIPipelineCLIOverrides(),
            )

        self.assertEqual(resolved.enabled_stages, ("triage", "draft", "polish"))

    def test_temperature_defaults_and_stage_override(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  tuned:
    provider: openai
    default_temperature:
      triage: 0.1
      draft: 0.3
    stages:
      draft:
        model: gpt-draft
      triage:
        model: gpt-triage
        temperature: 0.05
""",
            )
            resolved = resolve_ai_pipeline_config(
                repo_root=repo_root,
                profile_slug="tuned",
                cli_overrides=AIPipelineCLIOverrides(),
            )

        self.assertEqual(resolved.stages["triage"].temperature, 0.05)
        self.assertEqual(resolved.stages["draft"].temperature, 0.3)
        self.assertEqual(resolved.stages["polish"].temperature, 0.0)

    def test_invalid_temperature_rejected(self) -> None:
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
        temperature: 3
""",
            )

            with self.assertRaises(ValueError) as ctx:
                resolve_ai_pipeline_config(
                    repo_root=repo_root,
                    profile_slug="bad",
                    cli_overrides=AIPipelineCLIOverrides(),
                )

        self.assertIn("temperature", str(ctx.exception))

    def test_max_output_tokens_defaults_and_stage_override(self) -> None:
        with TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            _write_profile(
                repo_root,
                """
profiles:
  tuned:
    provider: openai
    default_max_output_tokens:
      triage: 200
      draft:
        mode: dynamic_ratio
        ratio: 0.25
        min: 700
        max: 2100
    stages:
      draft:
        model: gpt-draft
      polish:
        model: gpt-polish
        max_output_tokens: 900
""",
            )
            resolved = resolve_ai_pipeline_config(
                repo_root=repo_root,
                profile_slug="tuned",
                cli_overrides=AIPipelineCLIOverrides(),
            )

        self.assertEqual(resolved.stages["triage"].max_output_tokens, 200)
        self.assertEqual(resolved.stages["polish"].max_output_tokens, 900)
        self.assertIsInstance(resolved.stages["draft"].max_output_tokens, AIDynamicRatioTokenBudget)

    def test_invalid_dynamic_ratio_shape_rejected(self) -> None:
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
        max_output_tokens:
          mode: dynamic_ratio
          ratio: 0.2
          min: 800
""",
            )

            with self.assertRaises(ValueError) as ctx:
                resolve_ai_pipeline_config(
                    repo_root=repo_root,
                    profile_slug="bad",
                    cli_overrides=AIPipelineCLIOverrides(),
                )

        self.assertIn("dynamic_ratio requires", str(ctx.exception))

    def test_dynamic_ratio_budget_clamps_min_and_max(self) -> None:
        policy = AIDynamicRatioTokenBudget(mode="dynamic_ratio", ratio=0.5, min=100, max=400)
        self.assertEqual(compute_max_output_tokens(policy, input_size=20), 100)
        self.assertEqual(compute_max_output_tokens(policy, input_size=500), 250)
        self.assertEqual(compute_max_output_tokens(policy, input_size=2000), 400)


if __name__ == "__main__":
    unittest.main()
