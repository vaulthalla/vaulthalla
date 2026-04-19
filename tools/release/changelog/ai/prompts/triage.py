from __future__ import annotations

from typing import Any


def build_triage_system_prompt() -> str:
    raise NotImplementedError("Triage prompting is deferred to Phase 5b.")


def build_triage_user_prompt(payload: dict[str, Any]) -> str:
    del payload
    raise NotImplementedError("Triage prompting is deferred to Phase 5b.")
