from __future__ import annotations

from dataclasses import dataclass
from typing import Literal


OPENAI_API_KEY_ENV_VAR = "OPENAI_API_KEY"
DEFAULT_AI_DRAFT_MODEL = "gpt-5.4-mini"
DEFAULT_AI_TRIAGE_MODEL = DEFAULT_AI_DRAFT_MODEL
DEFAULT_AI_POLISH_MODEL = DEFAULT_AI_DRAFT_MODEL
DEFAULT_OPENAI_COMPATIBLE_BASE_URL = "http://localhost:8888/v1"
DEFAULT_AI_PROVIDER_KIND = "openai"

AIProviderKind = Literal["openai", "openai-compatible"]


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
