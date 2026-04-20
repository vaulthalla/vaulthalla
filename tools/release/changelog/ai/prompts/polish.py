from __future__ import annotations

import json
from typing import Any


def build_polish_system_prompt() -> str:
    return (
        "You are a deterministic release editor performing a constrained polish pass. "
        "Polish means wording refinement only; never add meaning. "
        "Do not invent features, fixes, impact, categories, or caveats. "
        "Do not use speculative wording: likely, suggests, appears to, may indicate, could be interpreted as. "
        "Prefer shorter phrasing and preserve all factual intent. "
        "Return JSON only that matches the schema."
    )


def build_polish_user_prompt(draft_payload: dict[str, Any]) -> str:
    draft_json = json.dumps(draft_payload, indent=2, sort_keys=False)
    return (
        "Refine the draft below for clarity and concision.\n"
        "Allowed edits:\n"
        "- tighten wording and remove repetition\n"
        "- simplify phrasing without changing facts\n"
        "Forbidden edits:\n"
        "- adding new changes or claims not present in the draft\n"
        "- introducing new sections or categories that are not in the draft\n"
        "- dropping caveats or weak-signal cautions\n"
        "- adding narrative or promotional language\n"
        "- increasing verbosity\n"
        "Return JSON only.\n\n"
        "Draft input:\n"
        f"{draft_json}"
    )
