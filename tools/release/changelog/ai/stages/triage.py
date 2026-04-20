from __future__ import annotations

import json
from typing import Any

from tools.release.changelog.ai.config import (
    AIMaxOutputTokensPolicy,
    AIProviderKind,
    AIReasoningEffort,
    AIStructuredMode,
    DEFAULT_STAGE_MAX_OUTPUT_TOKENS,
    DEFAULT_AI_TRIAGE_MODEL,
    compute_max_output_tokens,
    estimate_input_size_units,
)
from tools.release.changelog.ai.contracts.triage import (
    AI_TRIAGE_RESPONSE_JSON_SCHEMA,
    AITriageResult,
    ai_triage_result_to_dict,
    parse_ai_triage_response,
)
from tools.release.changelog.ai.prompts.triage import build_triage_system_prompt, build_triage_user_prompt
from tools.release.changelog.ai.providers.base import StructuredJSONProvider
from tools.release.changelog.ai.providers.openai import OpenAIProvider


def run_triage_stage(
    payload: dict[str, Any],
    *,
    model: str | None = None,
    provider: StructuredJSONProvider | None = None,
    provider_kind: AIProviderKind = "openai",
    reasoning_effort: AIReasoningEffort | None = None,
    structured_mode: AIStructuredMode | None = None,
    temperature: float | None = None,
    max_output_tokens_policy: AIMaxOutputTokensPolicy | None = None,
) -> AITriageResult:
    active_provider = provider or OpenAIProvider(model=model or DEFAULT_AI_TRIAGE_MODEL, provider_kind=provider_kind)
    system_prompt = build_triage_system_prompt()
    user_prompt = build_triage_user_prompt(payload)
    policy = max_output_tokens_policy or DEFAULT_STAGE_MAX_OUTPUT_TOKENS["triage"]
    max_output_tokens = compute_max_output_tokens(
        policy,
        input_size=estimate_input_size_units(system_prompt, user_prompt),
    )
    structured = active_provider.generate_structured_json(
        system_prompt=system_prompt,
        user_prompt=user_prompt,
        json_schema=AI_TRIAGE_RESPONSE_JSON_SCHEMA,
        reasoning_effort=reasoning_effort,
        structured_mode=structured_mode,
        temperature=temperature,
        max_output_tokens=max_output_tokens,
    )
    return parse_ai_triage_response(structured)


def render_triage_result_json(result: AITriageResult) -> str:
    return json.dumps(ai_triage_result_to_dict(result), indent=2, sort_keys=False) + "\n"
