from __future__ import annotations

import json
from typing import Any


def build_polish_system_prompt() -> str:
    return (
        "You are a release editor performing a polish pass on an existing draft. "
        "Your job is editorial refinement only: improve clarity, flow, and concision. "
        "Do not invent features, fixes, behaviors, impact, or categories. "
        "Do not add unsupported claims. Preserve factual meaning and caution notes. "
        "Return structured JSON only, matching the required schema exactly."
    )


def build_polish_user_prompt(draft_payload: dict[str, Any]) -> str:
    draft_json = json.dumps(draft_payload, indent=2, sort_keys=False)
    return (
        "Refine the draft below for readability and flow.\n"
        "Allowed edits:\n"
        "- tighten wording and reduce repetition\n"
        "- improve transitions and section coherence\n"
        "- clarify awkward phrasing while preserving meaning\n"
        "Forbidden edits:\n"
        "- adding new changes or claims not present in the draft\n"
        "- introducing new sections or categories that are not in the draft\n"
        "- dropping critical caveats or weak-signal cautions\n"
        "- introducing marketing language or dramatic reinterpretation\n"
        "Return JSON only.\n\n"
        "Draft input:\n"
        f"{draft_json}"
    )
