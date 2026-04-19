from __future__ import annotations

import json
import os
from typing import Any

from tools.release.changelog.ai.config import DEFAULT_AI_DRAFT_MODEL, OPENAI_API_KEY_ENV_VAR


class OpenAIProvider:
    """Thin OpenAI transport adapter for one structured JSON generation pass."""

    def __init__(
        self,
        *,
        model: str = DEFAULT_AI_DRAFT_MODEL,
        api_key: str | None = None,
        sdk_client: Any | None = None,
    ) -> None:
        self.model = model

        if sdk_client is not None:
            self._client = sdk_client
            return

        resolved_api_key = api_key or os.getenv(OPENAI_API_KEY_ENV_VAR)
        if not resolved_api_key:
            raise ValueError(
                f"{OPENAI_API_KEY_ENV_VAR} is not set. Export {OPENAI_API_KEY_ENV_VAR} to run `changelog ai-draft`."
            )

        self._client = _build_sdk_client(api_key=resolved_api_key)

    def generate_structured_json(
        self,
        *,
        system_prompt: str,
        user_prompt: str,
        json_schema: dict[str, Any],
    ) -> dict[str, Any]:
        try:
            response = self._client.chat.completions.create(
                **self._build_request(
                    model=self.model,
                    system_prompt=system_prompt,
                    user_prompt=user_prompt,
                    json_schema=json_schema,
                )
            )
        except Exception as exc:
            raise ValueError(f"OpenAI request failed: {exc}") from exc

        content = _extract_message_content(response)
        try:
            parsed = json.loads(content)
        except json.JSONDecodeError as exc:
            raise ValueError(f"OpenAI returned invalid JSON output: {exc}") from exc

        if not isinstance(parsed, dict):
            raise ValueError("OpenAI structured response must be a JSON object.")
        return parsed

    @staticmethod
    def _build_request(
        *,
        model: str,
        system_prompt: str,
        user_prompt: str,
        json_schema: dict[str, Any],
    ) -> dict[str, Any]:
        return {
            "model": model,
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_prompt},
            ],
            "response_format": {
                "type": "json_schema",
                "json_schema": {
                    "name": "vaulthalla_release_changelog_draft",
                    "schema": json_schema,
                    "strict": True,
                },
            },
        }


def _build_sdk_client(*, api_key: str) -> Any:
    try:
        from openai import OpenAI
    except Exception as exc:
        raise ValueError(
            "OpenAI SDK is not available in this environment. Install `openai` in the active venv."
        ) from exc
    return OpenAI(api_key=api_key)


def _extract_message_content(response: Any) -> str:
    choices = getattr(response, "choices", None)
    if not choices:
        raise ValueError("OpenAI response contained no choices.")

    message = getattr(choices[0], "message", None)
    if message is None:
        raise ValueError("OpenAI response choice contained no message.")

    refusal = getattr(message, "refusal", None)
    if refusal:
        raise ValueError(f"OpenAI refused the request: {refusal}")

    content = getattr(message, "content", None)
    if isinstance(content, str) and content.strip():
        return content

    # Some SDK variants may return content blocks instead of a raw string.
    if isinstance(content, list):
        blocks: list[str] = []
        for block in content:
            if isinstance(block, dict):
                text = block.get("text")
            else:
                text = getattr(block, "text", None)
            if isinstance(text, str) and text.strip():
                blocks.append(text)
        if blocks:
            return "".join(blocks)

    raise ValueError("OpenAI response did not include structured JSON content.")
