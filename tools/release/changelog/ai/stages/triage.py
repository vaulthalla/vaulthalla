from __future__ import annotations

import json
from typing import Any

from tools.release.changelog.ai.config import DEFAULT_AI_TRIAGE_MODEL
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
) -> AITriageResult:
    active_provider = provider or OpenAIProvider(model=model or DEFAULT_AI_TRIAGE_MODEL)
    structured = active_provider.generate_structured_json(
        system_prompt=build_triage_system_prompt(),
        user_prompt=build_triage_user_prompt(payload),
        json_schema=AI_TRIAGE_RESPONSE_JSON_SCHEMA,
    )
    return parse_ai_triage_response(structured)


def render_triage_result_json(result: AITriageResult) -> str:
    return json.dumps(ai_triage_result_to_dict(result), indent=2, sort_keys=False) + "\n"
