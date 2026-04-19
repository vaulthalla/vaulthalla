from tools.release.changelog.ai.config import (
    DEFAULT_AI_DRAFT_MODEL,
    DEFAULT_AI_POLISH_MODEL,
    DEFAULT_AI_TRIAGE_MODEL,
)
from tools.release.changelog.ai.contracts import (
    AIDraftResult,
    AIDraftSection,
    AI_DRAFT_RESPONSE_JSON_SCHEMA,
    AI_POLISH_RESPONSE_JSON_SCHEMA,
    AI_POLISH_SCHEMA_VERSION,
    AI_TRIAGE_RESPONSE_JSON_SCHEMA,
    AI_TRIAGE_SCHEMA_VERSION,
    AIPolishResult,
    AIPolishSection,
    AITriageCategory,
    AITriageResult,
    ai_draft_result_to_dict,
    ai_polish_result_to_dict,
    ai_triage_result_to_dict,
    build_polish_input_payload,
    build_triage_ir_payload,
    parse_ai_draft_response,
    parse_ai_polish_response,
    parse_ai_triage_response,
)
from tools.release.changelog.ai.providers.openai import OpenAIProvider
from tools.release.changelog.ai.render.markdown import render_draft_markdown, render_polish_markdown
from tools.release.changelog.ai.stages.draft import generate_draft_from_payload, render_draft_result_json
from tools.release.changelog.ai.stages.polish import render_polish_result_json, run_polish_stage
from tools.release.changelog.ai.stages.triage import render_triage_result_json, run_triage_stage

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
    "AIPolishSection",
    "AIPolishResult",
    "AI_POLISH_SCHEMA_VERSION",
    "AI_POLISH_RESPONSE_JSON_SCHEMA",
    "parse_ai_polish_response",
    "ai_polish_result_to_dict",
    "build_polish_input_payload",
    "AITriageCategory",
    "AITriageResult",
    "AI_TRIAGE_SCHEMA_VERSION",
    "AI_TRIAGE_RESPONSE_JSON_SCHEMA",
    "parse_ai_triage_response",
    "ai_triage_result_to_dict",
    "build_triage_ir_payload",
    "DEFAULT_AI_DRAFT_MODEL",
    "DEFAULT_AI_POLISH_MODEL",
    "DEFAULT_AI_TRIAGE_MODEL",
    "OpenAIProvider",
    "generate_draft_from_payload",
    "render_draft_markdown",
    "render_draft_result_json",
    "run_polish_stage",
    "render_polish_markdown",
    "render_polish_result_json",
    "run_triage_stage",
    "render_triage_result_json",
    "DEFAULT_OPENAI_MINI_MODEL",
    "OpenAIClientAdapter",
    "generate_mini_draft_from_payload",
    "render_ai_draft_markdown",
    "render_ai_draft_json",
]
