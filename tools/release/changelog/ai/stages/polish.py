from __future__ import annotations

import json
from typing import Any

from tools.release.changelog.ai.config import (
    AIProviderKind,
    AIReasoningEffort,
    AIStructuredMode,
    DEFAULT_AI_POLISH_MODEL,
)
from tools.release.changelog.ai.contracts.draft import AIDraftResult
from tools.release.changelog.ai.contracts.polish import (
    AI_POLISH_RESPONSE_JSON_SCHEMA,
    AIPolishResult,
    ai_polish_result_to_dict,
    build_polish_input_payload,
    parse_ai_polish_response,
)
from tools.release.changelog.ai.prompts.polish import build_polish_system_prompt, build_polish_user_prompt
from tools.release.changelog.ai.providers.base import StructuredJSONProvider
from tools.release.changelog.ai.providers.openai import OpenAIProvider


def run_polish_stage(
    draft: AIDraftResult,
    *,
    model: str | None = None,
    provider: StructuredJSONProvider | None = None,
    provider_kind: AIProviderKind = "openai",
    reasoning_effort: AIReasoningEffort | None = None,
    structured_mode: AIStructuredMode | None = None,
) -> AIPolishResult:
    active_provider = provider or OpenAIProvider(model=model or DEFAULT_AI_POLISH_MODEL, provider_kind=provider_kind)
    draft_payload = build_polish_input_payload(draft)
    structured = active_provider.generate_structured_json(
        system_prompt=build_polish_system_prompt(),
        user_prompt=build_polish_user_prompt(draft_payload),
        json_schema=AI_POLISH_RESPONSE_JSON_SCHEMA,
        reasoning_effort=reasoning_effort,
        structured_mode=structured_mode,
    )
    return parse_ai_polish_response(structured)


def render_polish_result_json(result: AIPolishResult) -> str:
    return json.dumps(ai_polish_result_to_dict(result), indent=2, sort_keys=False) + "\n"
