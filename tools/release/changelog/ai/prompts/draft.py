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


def build_draft_user_prompt(payload: dict[str, Any]) -> str:
    payload_json = json.dumps(payload, indent=2, sort_keys=False)
    return (
        "Draft a human-reviewable release summary from the payload below.\n"
        "Requirements:\n"
        "- Keep claims evidence-forward and concise.\n"
        "- Include one section for each meaningful category in the payload.\n"
        "- Avoid repeating identical facts across bullets.\n"
        "- If category evidence is weak, note that plainly.\n"
        "- Return JSON only.\n\n"
        "Release payload:\n"
        f"{payload_json}"
    )
