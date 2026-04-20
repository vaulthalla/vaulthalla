from __future__ import annotations

import json
from typing import Any

from tools.release.changelog.ai.config import DEFAULT_AI_DRAFT_MODEL
from tools.release.changelog.ai.contracts.draft import (
    AI_DRAFT_RESPONSE_JSON_SCHEMA,
    AIDraftResult,
    ai_draft_result_to_dict,
    parse_ai_draft_response,
)
from tools.release.changelog.ai.prompts.draft import build_draft_system_prompt, build_draft_user_prompt
from tools.release.changelog.ai.providers.base import StructuredJSONProvider
from tools.release.changelog.ai.providers.openai import OpenAIProvider


def generate_draft_from_payload(
    payload: dict[str, Any],
    *,
    model: str | None = None,
    provider: StructuredJSONProvider | None = None,
    source_kind: str = "payload",
) -> AIDraftResult:
    active_provider = provider or OpenAIProvider(model=model or DEFAULT_AI_DRAFT_MODEL)
    structured = active_provider.generate_structured_json(
        system_prompt=build_draft_system_prompt(),
        user_prompt=build_draft_user_prompt(payload, source_kind=source_kind),
        json_schema=AI_DRAFT_RESPONSE_JSON_SCHEMA,
    )
    return parse_ai_draft_response(structured)


def render_draft_result_json(draft: AIDraftResult) -> str:
    return json.dumps(ai_draft_result_to_dict(draft), indent=2, sort_keys=False) + "\n"
