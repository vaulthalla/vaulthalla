from __future__ import annotations

import json
from typing import Any


def build_triage_system_prompt() -> str:
    return (
        "You are a release triage optimizer for Vaulthalla. "
        "You compress and prioritize evidence before drafting. "
        "Use only provided evidence and never invent features, fixes, behavior, or impact. "
        "Prefer user-visible and operationally significant changes. "
        "Mark weak categories cautiously. "
        "Return JSON only, matching the required schema exactly."
    )


def build_triage_user_prompt(payload: dict[str, Any]) -> str:
    payload_json = json.dumps(payload, indent=2, sort_keys=False)
    return (
        "Build a compact triage intermediate representation from the release payload.\n"
        "Requirements:\n"
        "- Keep only high-signal evidence needed for downstream drafting.\n"
        "- Rank categories by importance using `priority_rank` (1 is highest).\n"
        "- For each kept category include concise `key_points`, important files, and retained snippets.\n"
        "- Keep weak categories factual and include caution notes when appropriate.\n"
        "- Record dropped low-signal details in `dropped_noise` where useful.\n"
        "- Return JSON only.\n\n"
        "Release payload:\n"
        f"{payload_json}"
    )
