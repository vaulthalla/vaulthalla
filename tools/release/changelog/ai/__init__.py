from tools.release.changelog.ai.config import DEFAULT_AI_DRAFT_MODEL
from tools.release.changelog.ai.contracts import (
    AIDraftResult,
    AIDraftSection,
    AI_DRAFT_RESPONSE_JSON_SCHEMA,
    ai_draft_result_to_dict,
    parse_ai_draft_response,
)
from tools.release.changelog.ai.providers.openai import OpenAIProvider
from tools.release.changelog.ai.render.markdown import render_draft_markdown
from tools.release.changelog.ai.stages.draft import generate_draft_from_payload, render_draft_result_json

# Backward-compatible aliases while imports migrate to responsibility modules.
DEFAULT_OPENAI_MINI_MODEL = DEFAULT_AI_DRAFT_MODEL
OpenAIClientAdapter = OpenAIProvider
generate_mini_draft_from_payload = generate_draft_from_payload
render_ai_draft_markdown = render_draft_markdown
render_ai_draft_json = render_draft_result_json

__all__ = [
    "AIDraftSection",
    "AIDraftResult",
    "AI_DRAFT_RESPONSE_JSON_SCHEMA",
    "parse_ai_draft_response",
    "ai_draft_result_to_dict",
    "DEFAULT_AI_DRAFT_MODEL",
    "OpenAIProvider",
    "generate_draft_from_payload",
    "render_draft_markdown",
    "render_draft_result_json",
    "DEFAULT_OPENAI_MINI_MODEL",
    "OpenAIClientAdapter",
    "generate_mini_draft_from_payload",
    "render_ai_draft_markdown",
    "render_ai_draft_json",
]
