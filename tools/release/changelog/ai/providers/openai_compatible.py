from __future__ import annotations

from typing import Any

from tools.release.changelog.ai.providers.base import StructuredJSONProvider


class OpenAICompatibleProvider(StructuredJSONProvider):
    """Placeholder for future local OpenAI-compatible providers."""

    def __init__(self, *, model: str, base_url: str, api_key: str | None = None, sdk_client: Any | None = None):
        self.model = model
        self.base_url = base_url
        self.api_key = api_key
        self._client = sdk_client

    def generate_structured_json(
        self,
        *,
        system_prompt: str,
        user_prompt: str,
        json_schema: dict[str, Any],
    ) -> dict[str, Any]:
        raise NotImplementedError("OpenAI-compatible provider support is deferred to a later phase.")
