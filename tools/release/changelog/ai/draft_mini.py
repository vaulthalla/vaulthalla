from __future__ import annotations

import json
from typing import Any

from tools.release.changelog.ai.contracts import (
    AI_DRAFT_RESPONSE_JSON_SCHEMA,
    AIDraftResult,
    ai_draft_result_to_dict,
    parse_ai_draft_response,
)
from tools.release.changelog.ai.openai_client import DEFAULT_OPENAI_MINI_MODEL, OpenAIClientAdapter
from tools.release.changelog.ai.prompt_mini import build_mini_system_prompt, build_mini_user_prompt


def generate_mini_draft_from_payload(
    payload: dict[str, Any],
    *,
    model: str | None = None,
    client: OpenAIClientAdapter | None = None,
) -> AIDraftResult:
    active_client = client or OpenAIClientAdapter(model=model or DEFAULT_OPENAI_MINI_MODEL)
    structured = active_client.generate_structured_json(
        system_prompt=build_mini_system_prompt(),
        user_prompt=build_mini_user_prompt(payload),
        json_schema=AI_DRAFT_RESPONSE_JSON_SCHEMA,
    )
    return parse_ai_draft_response(structured)


def render_ai_draft_markdown(draft: AIDraftResult) -> str:
    lines = [f"# {draft.title}", "", draft.summary, ""]

    for section in draft.sections:
        lines.append(f"## {section.category.title()}")
        lines.append(section.overview)
        lines.append("")
        for bullet in section.bullets:
            lines.append(f"- {bullet}")
        lines.append("")

    if draft.notes:
        lines.append("## Notes")
        for note in draft.notes:
            lines.append(f"- {note}")
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def render_ai_draft_json(draft: AIDraftResult) -> str:
    return json.dumps(ai_draft_result_to_dict(draft), indent=2, sort_keys=False) + "\n"
