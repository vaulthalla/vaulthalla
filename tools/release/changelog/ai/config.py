from __future__ import annotations

from dataclasses import dataclass


OPENAI_API_KEY_ENV_VAR = "OPENAI_API_KEY"
DEFAULT_AI_DRAFT_MODEL = "gpt-5.4-mini"
DEFAULT_AI_TRIAGE_MODEL = DEFAULT_AI_DRAFT_MODEL
DEFAULT_AI_POLISH_MODEL = DEFAULT_AI_DRAFT_MODEL


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
