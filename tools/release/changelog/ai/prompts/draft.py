from __future__ import annotations

import json
from typing import Any


def build_draft_system_prompt() -> str:
    return (
        "You are a release editor for Vaulthalla. "
        "Use only provided evidence. Do not invent fixes, features, or impacts. "
        "Treat weak-signal categories cautiously and state uncertainty directly. "
        "Return structured JSON only, matching the required schema exactly."
    )


def build_draft_user_prompt(source_data: dict[str, Any], *, source_kind: str = "payload") -> str:
    source_json = json.dumps(source_data, indent=2, sort_keys=False)
    input_name = "Triage IR" if source_kind == "triage" else "Release payload"
    return (
        "Draft a human-reviewable release summary from the structured input below.\n"
        "Requirements:\n"
        "- Keep claims evidence-forward and concise.\n"
        "- Include one section for each meaningful category in the input.\n"
        "- Avoid repeating identical facts across bullets.\n"
        "- If category evidence is weak, note that plainly.\n"
        "- Return JSON only.\n\n"
        f"{input_name}:\n"
        f"{source_json}"
    )
