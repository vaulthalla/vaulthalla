from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Literal

import yaml

OPENAI_API_KEY_ENV_VAR = "OPENAI_API_KEY"
DEFAULT_AI_DRAFT_MODEL = "gpt-5.4-mini"
DEFAULT_AI_TRIAGE_MODEL = DEFAULT_AI_DRAFT_MODEL
DEFAULT_AI_POLISH_MODEL = DEFAULT_AI_DRAFT_MODEL
DEFAULT_OPENAI_COMPATIBLE_BASE_URL = "http://localhost:8888/v1"
DEFAULT_AI_PROVIDER_KIND = "openai"

VALID_REASONING_EFFORTS = ("low", "medium", "high")
VALID_STRUCTURED_MODES = ("strict_json_schema", "json_object", "prompt_json")

AIProviderKind = Literal["openai", "openai-compatible"]
AIReasoningEffort = Literal["low", "medium", "high"]
AIStructuredMode = Literal["strict_json_schema", "json_object", "prompt_json"]


@dataclass(frozen=True)
class AIDraftStageConfig:
    model: str = DEFAULT_AI_DRAFT_MODEL
    api_key_env_var: str = OPENAI_API_KEY_ENV_VAR


@dataclass(frozen=True)
class AITriageStageConfig:
    model: str = DEFAULT_AI_TRIAGE_MODEL
    api_key_env_var: str = OPENAI_API_KEY_ENV_VAR


@dataclass(frozen=True)
class AIPolishStageConfig:
    model: str = DEFAULT_AI_POLISH_MODEL
    api_key_env_var: str = OPENAI_API_KEY_ENV_VAR


@dataclass(frozen=True)
class AIProviderConfig:
    kind: AIProviderKind = DEFAULT_AI_PROVIDER_KIND
    model: str = DEFAULT_AI_DRAFT_MODEL
    base_url: str | None = None
    api_key_env_var: str = OPENAI_API_KEY_ENV_VAR
    api_key: str | None = None
    timeout_seconds: float | None = None


AIStageName = Literal["triage", "draft", "polish"]


@dataclass(frozen=True)
class AIPipelineStageConfig:
    model: str
    reasoning_effort: AIReasoningEffort | None = None
    structured_mode: AIStructuredMode | None = None


@dataclass(frozen=True)
class AIPipelineConfig:
    provider: AIProviderKind
    base_url: str | None
    fallback_model: str
    stages: dict[AIStageName, AIPipelineStageConfig]
    profile_slug: str | None = None

    def stage_model(self, stage: AIStageName) -> str:
        return self.stages[stage].model

    def provider_config_for_stage(self, stage: AIStageName) -> AIProviderConfig:
        return AIProviderConfig(
            kind=self.provider,
            model=self.stage_model(stage),
            base_url=self.base_url,
        )


@dataclass(frozen=True)
class AIPipelineCLIOverrides:
    provider: AIProviderKind | None = None
    base_url: str | None = None
    model: str | None = None


DEFAULT_AI_PROFILE_PATH = Path(".vaulthalla/ai.yml")


def resolve_ai_pipeline_config(
    *,
    repo_root: Path,
    profile_slug: str | None,
    cli_overrides: AIPipelineCLIOverrides,
) -> AIPipelineConfig:
    provider: AIProviderKind = DEFAULT_AI_PROVIDER_KIND
    base_url: str | None = None
    fallback_model = DEFAULT_AI_DRAFT_MODEL
    stage_models: dict[AIStageName, str | None] = {
        "triage": DEFAULT_AI_TRIAGE_MODEL,
        "draft": DEFAULT_AI_DRAFT_MODEL,
        "polish": DEFAULT_AI_POLISH_MODEL,
    }
    stage_reasoning: dict[AIStageName, AIReasoningEffort | None] = {
        "triage": None,
        "draft": None,
        "polish": None,
    }
    stage_structured_modes: dict[AIStageName, AIStructuredMode | None] = {
        "triage": None,
        "draft": None,
        "polish": None,
    }

    if profile_slug:
        profile = _load_profile(repo_root / DEFAULT_AI_PROFILE_PATH, profile_slug)
        provider = _read_provider(profile.get("provider"), path=f"profiles.{profile_slug}.provider")
        base_url = _read_optional_non_empty_string(profile.get("base_url"), path=f"profiles.{profile_slug}.base_url")
        explicitly_configured_stages: set[AIStageName] = set()
        profile_fallback = _read_optional_non_empty_string(profile.get("model"), path=f"profiles.{profile_slug}.model")
        if profile_fallback is not None:
            fallback_model = profile_fallback
        profile_reasoning_fallback = _read_optional_reasoning_effort(
            profile.get("reasoning_effort"),
            path=f"profiles.{profile_slug}.reasoning_effort",
        )
        profile_structured_mode_fallback = _read_optional_structured_mode(
            profile.get("structured_mode"),
            path=f"profiles.{profile_slug}.structured_mode",
        )

        stages = profile.get("stages")
        if stages is not None:
            if not isinstance(stages, dict):
                raise ValueError(f"Invalid AI profile `profiles.{profile_slug}.stages`: expected mapping.")
            for stage_name, stage_cfg in stages.items():
                if stage_name not in {"triage", "draft", "polish"}:
                    raise ValueError(
                        f"Invalid AI profile `profiles.{profile_slug}.stages.{stage_name}`: unknown stage."
                    )
                if not isinstance(stage_cfg, dict):
                    raise ValueError(
                        f"Invalid AI profile `profiles.{profile_slug}.stages.{stage_name}`: expected mapping."
                    )
                model = _read_optional_non_empty_string(
                    stage_cfg.get("model"),
                    path=f"profiles.{profile_slug}.stages.{stage_name}.model",
                )
                if model is not None:
                    stage_models[stage_name] = model
                    explicitly_configured_stages.add(stage_name)
                stage_reasoning[stage_name] = _read_optional_reasoning_effort(
                    stage_cfg.get("reasoning_effort"),
                    path=f"profiles.{profile_slug}.stages.{stage_name}.reasoning_effort",
                )
                stage_structured_modes[stage_name] = _read_optional_structured_mode(
                    stage_cfg.get("structured_mode"),
                    path=f"profiles.{profile_slug}.stages.{stage_name}.structured_mode",
                )

        if profile_fallback is not None:
            for stage_name in ("triage", "draft", "polish"):
                if stage_name not in explicitly_configured_stages:
                    stage_models[stage_name] = profile_fallback
        if profile_reasoning_fallback is not None:
            for stage_name in ("triage", "draft", "polish"):
                if stage_reasoning[stage_name] is None:
                    stage_reasoning[stage_name] = profile_reasoning_fallback
        if profile_structured_mode_fallback is not None:
            for stage_name in ("triage", "draft", "polish"):
                if stage_structured_modes[stage_name] is None:
                    stage_structured_modes[stage_name] = profile_structured_mode_fallback

    if cli_overrides.provider is not None:
        provider = cli_overrides.provider

    if cli_overrides.base_url is not None:
        base_url = _normalize_optional_string(cli_overrides.base_url)

    cli_model = _read_optional_non_empty_string(cli_overrides.model, path="cli.model")
    if cli_model is not None:
        fallback_model = cli_model
        stage_models["triage"] = cli_model
        stage_models["draft"] = cli_model
        stage_models["polish"] = cli_model

    resolved_stages: dict[AIStageName, AIPipelineStageConfig] = {
        "triage": AIPipelineStageConfig(
            model=stage_models["triage"] or fallback_model,
            reasoning_effort=stage_reasoning["triage"],
            structured_mode=stage_structured_modes["triage"],
        ),
        "draft": AIPipelineStageConfig(
            model=stage_models["draft"] or fallback_model,
            reasoning_effort=stage_reasoning["draft"],
            structured_mode=stage_structured_modes["draft"],
        ),
        "polish": AIPipelineStageConfig(
            model=stage_models["polish"] or fallback_model,
            reasoning_effort=stage_reasoning["polish"],
            structured_mode=stage_structured_modes["polish"],
        ),
    }

    return AIPipelineConfig(
        provider=provider,
        base_url=base_url,
        fallback_model=fallback_model,
        stages=resolved_stages,
        profile_slug=profile_slug,
    )


def _load_profile(path: Path, profile_slug: str) -> dict:
    if not path.is_file():
        raise ValueError(
            f"AI profile `{profile_slug}` requested but config file is missing: {path}"
        )

    try:
        raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    except yaml.YAMLError as exc:
        raise ValueError(f"Invalid AI profile config YAML at {path}: {exc}") from exc
    except Exception as exc:
        raise ValueError(f"Failed to read AI profile config at {path}: {exc}") from exc

    if raw is None:
        raw = {}
    if not isinstance(raw, dict):
        raise ValueError(f"Invalid AI profile config at {path}: root must be a mapping.")

    profiles = raw.get("profiles")
    if not isinstance(profiles, dict):
        raise ValueError(f"Invalid AI profile config at {path}: `profiles` must be a mapping.")

    if profile_slug not in profiles:
        available = ", ".join(sorted(profiles.keys())) or "<none>"
        raise ValueError(
            f"Unknown AI profile `{profile_slug}` in {path}. Available profiles: {available}"
        )

    profile = profiles[profile_slug]
    if not isinstance(profile, dict):
        raise ValueError(f"Invalid AI profile `profiles.{profile_slug}`: expected mapping.")
    return profile


def _read_provider(raw: object, *, path: str) -> AIProviderKind:
    if not isinstance(raw, str) or not raw.strip():
        raise ValueError(f"Invalid AI profile `{path}`: expected non-empty provider string.")
    provider = raw.strip()
    if provider not in {"openai", "openai-compatible"}:
        raise ValueError(f"Invalid AI profile `{path}`: unsupported provider `{provider}`.")
    return provider  # type: ignore[return-value]


def _normalize_optional_string(raw: str | None) -> str | None:
    if raw is None:
        return None
    normalized = raw.strip()
    return normalized or None


def _read_optional_non_empty_string(raw: object, *, path: str) -> str | None:
    if raw is None:
        return None
    if not isinstance(raw, str):
        raise ValueError(f"Invalid `{path}`: expected string.")
    normalized = raw.strip()
    if not normalized:
        raise ValueError(f"Invalid `{path}`: expected non-empty string.")
    return normalized


def _read_optional_reasoning_effort(raw: object, *, path: str) -> AIReasoningEffort | None:
    if raw is None:
        return None
    if not isinstance(raw, str):
        raise ValueError(f"Invalid `{path}`: expected string.")
    normalized = raw.strip()
    if normalized not in VALID_REASONING_EFFORTS:
        allowed = ", ".join(VALID_REASONING_EFFORTS)
        raise ValueError(f"Invalid `{path}`: expected one of {allowed}.")
    return normalized  # type: ignore[return-value]


def _read_optional_structured_mode(raw: object, *, path: str) -> AIStructuredMode | None:
    if raw is None:
        return None
    if not isinstance(raw, str):
        raise ValueError(f"Invalid `{path}`: expected string.")
    normalized = raw.strip()
    if normalized not in VALID_STRUCTURED_MODES:
        allowed = ", ".join(VALID_STRUCTURED_MODES)
        raise ValueError(f"Invalid `{path}`: expected one of {allowed}.")
    return normalized  # type: ignore[return-value]
