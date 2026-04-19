from __future__ import annotations

from typing import Any, Protocol


class StructuredJSONProvider(Protocol):
    def generate_structured_json(
        self,
        *,
        system_prompt: str,
        user_prompt: str,
        json_schema: dict[str, Any],
    ) -> dict[str, Any]:
        ...
