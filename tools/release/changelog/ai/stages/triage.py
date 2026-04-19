from __future__ import annotations

from typing import Any


def run_triage_stage(payload: dict[str, Any]) -> dict[str, Any]:
    del payload
    raise NotImplementedError("Triage stage orchestration is deferred to Phase 5b.")
