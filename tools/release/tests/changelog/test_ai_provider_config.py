from __future__ import annotations

import unittest
from unittest.mock import patch

from tools.release.changelog.ai.config import (
    AIProviderConfig,
    DEFAULT_OPENAI_COMPATIBLE_BASE_URL,
)
from tools.release.changelog.ai.providers import build_structured_json_provider


class AIProviderConfigTests(unittest.TestCase):
    def test_build_openai_provider(self) -> None:
        sentinel = object()
        with patch("tools.release.changelog.ai.providers.OpenAIProvider", return_value=sentinel) as openai_cls:
            provider = build_structured_json_provider(
                AIProviderConfig(kind="openai", model="gpt-5.4-mini")
            )

        self.assertIs(provider, sentinel)
        openai_cls.assert_called_once_with(
            model="gpt-5.4-mini",
            provider_kind="openai",
            api_key=None,
            api_key_env_var="OPENAI_API_KEY",
            timeout_seconds=None,
        )

    def test_build_openai_compatible_provider_uses_default_local_base_url(self) -> None:
        sentinel = object()
        with patch(
            "tools.release.changelog.ai.providers.OpenAICompatibleProvider",
            return_value=sentinel,
        ) as compat_cls:
            provider = build_structured_json_provider(
                AIProviderConfig(kind="openai-compatible", model="Qwen3.5-122B")
            )

        self.assertIs(provider, sentinel)
        compat_cls.assert_called_once_with(
            model="Qwen3.5-122B",
            base_url=DEFAULT_OPENAI_COMPATIBLE_BASE_URL,
            api_key=None,
            api_key_env_var="OPENAI_API_KEY",
            timeout_seconds=None,
        )

    def test_openai_provider_rejects_base_url(self) -> None:
        with self.assertRaisesRegex(ValueError, "base-url"):
            build_structured_json_provider(
                AIProviderConfig(
                    kind="openai",
                    model="gpt-5.4-mini",
                    base_url="http://localhost:8888/v1",
                )
            )


if __name__ == "__main__":
    unittest.main()
