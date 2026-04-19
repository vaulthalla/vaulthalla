from __future__ import annotations

from dataclasses import dataclass


OPENAI_API_KEY_ENV_VAR = "OPENAI_API_KEY"
DEFAULT_AI_DRAFT_MODEL = "gpt-5.4-mini"


@dataclass(frozen=True)
class AIDraftStageConfig:
    model: str = DEFAULT_AI_DRAFT_MODEL
    api_key_env_var: str = OPENAI_API_KEY_ENV_VAR
