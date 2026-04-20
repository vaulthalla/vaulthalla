from __future__ import annotations

import json
from typing import Any


def build_triage_system_prompt() -> str:
    return (
        "You are a deterministic release triage classifier for Vaulthalla. "
        "Use only explicit input evidence. "
        "Never invent features, fixes, behavior, impact, or categories. "
        "Do not use speculative wording: likely, suggests, appears to, may indicate, could be interpreted as. "
        "If evidence is weak, state that briefly in caution fields. "
        "Return JSON only that matches the schema."
    )


def build_triage_user_prompt(payload: dict[str, Any]) -> str:
    payload_json = json.dumps(payload, indent=2, sort_keys=False)
    return (
        "Build a compact triage intermediate representation from the release payload.\n"
        "Requirements:\n"
        "- Keep only evidence required for downstream drafting.\n"
        "- Create a category only when at least one concrete supporting item exists.\n"
        "- Rank categories by `priority_rank` (1 is highest).\n"
        "- Keep `key_points` concise and evidence-bound.\n"
        "- No filler prose, no intro/outro text, no repetition.\n"
        "- Record dropped low-signal details in `dropped_noise`.\n"
        "- Return JSON only.\n\n"
        "Release payload:\n"
        f"{payload_json}"
    )
