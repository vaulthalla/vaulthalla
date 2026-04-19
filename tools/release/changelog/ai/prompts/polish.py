from __future__ import annotations

from typing import Any


def build_polish_system_prompt() -> str:
    raise NotImplementedError("Polish prompting is deferred to Phase 5c.")


def build_polish_user_prompt(payload: dict[str, Any]) -> str:
    del payload
    raise NotImplementedError("Polish prompting is deferred to Phase 5c.")
