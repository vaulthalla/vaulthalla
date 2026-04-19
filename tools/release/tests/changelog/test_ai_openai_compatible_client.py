from __future__ import annotations

import os
import unittest
from unittest.mock import patch

from tools.release.changelog.ai.providers.openai import LOCAL_NO_AUTH_API_KEY_PLACEHOLDER
from tools.release.changelog.ai.providers.openai_compatible import OpenAICompatibleProvider


class _FakeMessage:
    def __init__(self, content):
        self.content = content
        self.refusal = None


class _FakeChoice:
    def __init__(self, content):
        self.message = _FakeMessage(content)


class _FakeResponse:
    def __init__(self, content):
        self.choices = [_FakeChoice(content)]


class _FakeCompletions:
    def __init__(self, response):
        self._response = response
        self.calls: list[dict] = []

    def create(self, **kwargs):
        self.calls.append(kwargs)
        return self._response


class _FakeChat:
    def __init__(self, completions):
        self.completions = completions


class _FakeSDKClient:
    def __init__(self, response):
        self.chat = _FakeChat(_FakeCompletions(response))


class OpenAICompatibleProviderTests(unittest.TestCase):
    def test_missing_api_key_is_allowed_for_local_compatible_provider(self) -> None:
        fake_sdk = _FakeSDKClient(
            _FakeResponse(
                '{"title":"x","summary":"y","sections":[{"category":"core","overview":"z","bullets":["a"]}]}'
            )
        )
        with (
            patch.dict(os.environ, {}, clear=True),
            patch(
                "tools.release.changelog.ai.providers.openai._build_sdk_client",
                return_value=fake_sdk,
            ) as build_sdk,
        ):
            _ = OpenAICompatibleProvider(model="Qwen3.5-122B", base_url="http://localhost:8888/v1")

        build_sdk.assert_called_once_with(
            api_key=LOCAL_NO_AUTH_API_KEY_PLACEHOLDER,
            base_url="http://localhost:8888/v1",
            timeout_seconds=None,
        )

    def test_structured_request_uses_json_schema_response_format(self) -> None:
        sdk = _FakeSDKClient(
            _FakeResponse(
                '{"title":"x","summary":"y","sections":[{"category":"core","overview":"z","bullets":["a"]}]}'
            )
        )
        client = OpenAICompatibleProvider(
            sdk_client=sdk,
            model="Qwen3.5-122B",
            base_url="http://localhost:8888/v1",
        )

        result = client.generate_structured_json(
            system_prompt="sys",
            user_prompt="usr",
            json_schema={"type": "object"},
        )

        self.assertEqual(result["title"], "x")
        call = sdk.chat.completions.calls[0]
        self.assertEqual(call["model"], "Qwen3.5-122B")
        self.assertEqual(call["response_format"]["type"], "json_schema")
        self.assertTrue(call["response_format"]["json_schema"]["strict"])
        self.assertEqual(call["response_format"]["json_schema"]["schema"], {"type": "object"})

    def test_missing_base_url_fails_clearly(self) -> None:
        with self.assertRaisesRegex(ValueError, "base_url"):
            OpenAICompatibleProvider(model="Qwen3.5-122B", base_url="")


if __name__ == "__main__":
    unittest.main()
