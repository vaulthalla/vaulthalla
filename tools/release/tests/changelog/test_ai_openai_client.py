from __future__ import annotations

import os
import unittest
from unittest.mock import patch

from tools.release.changelog.ai.providers.openai import OpenAIProvider


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
        self.error: Exception | None = None

    def create(self, **kwargs):
        self.calls.append(kwargs)
        if self.error is not None:
            raise self.error
        return self._response


class _FakeChat:
    def __init__(self, completions):
        self.completions = completions


class _FakeSDKClient:
    def __init__(self, response):
        self.chat = _FakeChat(_FakeCompletions(response))
        self.models = type("FakeModels", (), {"list": lambda self: {"data": []}})()


class _FallbackCompletions:
    def __init__(self, final_response):
        self._final_response = final_response
        self.calls: list[dict] = []

    def create(self, **kwargs):
        self.calls.append(kwargs)
        response_format = kwargs.get("response_format")
        if isinstance(response_format, dict) and response_format.get("type") in {"json_schema", "json_object"}:
            raise RuntimeError(f"response_format {response_format.get('type')} unsupported")
        return self._final_response


class _FakeResponses:
    def __init__(self, output_text: str):
        self.output_text = output_text
        self.calls: list[dict] = []

    def create(self, **kwargs):
        self.calls.append(kwargs)
        return {"output_text": self.output_text}


class _FakeSDKWithResponses:
    def __init__(self, output_text: str):
        self.responses = _FakeResponses(output_text)
        self.chat = _FakeChat(_FakeCompletions(_FakeResponse(output_text)))
        self.models = type("FakeModels", (), {"list": lambda self: {"data": []}})()


class OpenAIProviderTests(unittest.TestCase):
    def test_structured_request_uses_json_schema_response_format(self) -> None:
        sdk = _FakeSDKClient(_FakeResponse('{"title":"x","summary":"y","sections":[{"category":"core","overview":"z","bullets":["a"]}]}'))
        client = OpenAIProvider(sdk_client=sdk, model="gpt-test-mini")

        result = client.generate_structured_json(
            system_prompt="sys",
            user_prompt="usr",
            json_schema={"type": "object"},
        )

        self.assertEqual(result["title"], "x")
        call = sdk.chat.completions.calls[0]
        self.assertEqual(call["model"], "gpt-test-mini")
        self.assertEqual(call["response_format"]["type"], "json_schema")
        self.assertTrue(call["response_format"]["json_schema"]["strict"])
        self.assertEqual(call["response_format"]["json_schema"]["schema"], {"type": "object"})
        self.assertEqual(call["messages"][0]["role"], "system")
        self.assertEqual(call["messages"][1]["role"], "user")
        self.assertNotIn("reasoning", call)

    def test_reasoning_effort_is_forwarded_when_requested(self) -> None:
        sdk = _FakeSDKClient(_FakeResponse('{"title":"x","summary":"y","sections":[{"category":"core","overview":"z","bullets":["a"]}]}'))
        client = OpenAIProvider(sdk_client=sdk, model="gpt-test-mini")

        _ = client.generate_structured_json(
            system_prompt="sys",
            user_prompt="usr",
            json_schema={"type": "object"},
            reasoning_effort="high",
        )

        call = sdk.chat.completions.calls[0]
        self.assertEqual(call["reasoning"]["effort"], "high")

    def test_temperature_and_max_tokens_are_forwarded_when_requested(self) -> None:
        sdk = _FakeSDKClient(_FakeResponse('{"title":"x","summary":"y","sections":[{"category":"core","overview":"z","bullets":["a"]}]}'))
        client = OpenAIProvider(sdk_client=sdk, model="gpt-test-mini")

        _ = client.generate_structured_json(
            system_prompt="sys",
            user_prompt="usr",
            json_schema={"type": "object"},
            temperature=0.25,
            max_output_tokens=1024,
        )

        call = sdk.chat.completions.calls[0]
        self.assertEqual(call["temperature"], 0.25)
        self.assertEqual(call["max_completion_tokens"], 1024)

    def test_json_object_mode_uses_json_object_response_format(self) -> None:
        sdk = _FakeSDKClient(_FakeResponse('{"title":"x","summary":"y","sections":[{"category":"core","overview":"z","bullets":["a"]}]}'))
        client = OpenAIProvider(sdk_client=sdk, model="gpt-test-mini")

        _ = client.generate_structured_json(
            system_prompt="sys",
            user_prompt="usr",
            json_schema={"type": "object"},
            structured_mode="json_object",
        )

        call = sdk.chat.completions.calls[0]
        self.assertEqual(call["response_format"]["type"], "json_object")

    def test_prompt_json_mode_omits_response_format(self) -> None:
        sdk = _FakeSDKClient(_FakeResponse('{"title":"x","summary":"y","sections":[{"category":"core","overview":"z","bullets":["a"]}]}'))
        client = OpenAIProvider(sdk_client=sdk, model="gpt-test-mini")

        _ = client.generate_structured_json(
            system_prompt="sys",
            user_prompt="usr",
            json_schema={"type": "object"},
            structured_mode="prompt_json",
        )

        call = sdk.chat.completions.calls[0]
        self.assertNotIn("response_format", call)
        self.assertIn("valid JSON object", call["messages"][0]["content"])

    def test_provider_exposes_capabilities(self) -> None:
        sdk = _FakeSDKClient(_FakeResponse("{}"))
        client = OpenAIProvider(sdk_client=sdk)
        caps = client.capabilities()
        self.assertTrue(caps.supports_reasoning_effort)
        self.assertTrue(caps.supports_strict_schema)

    def test_responses_api_is_used_for_hosted_openai_when_available(self) -> None:
        sdk = _FakeSDKWithResponses(
            '{"title":"x","summary":"y","sections":[{"category":"core","overview":"z","bullets":["a"]}]}'
        )
        client = OpenAIProvider(sdk_client=sdk, model="gpt-test-mini")

        _ = client.generate_structured_json(
            system_prompt="sys",
            user_prompt="usr",
            json_schema={"type": "object"},
            reasoning_effort="medium",
            structured_mode="strict_json_schema",
            temperature=0.1,
            max_output_tokens=777,
        )

        self.assertEqual(len(sdk.responses.calls), 1)
        request = sdk.responses.calls[0]
        self.assertEqual(request["model"], "gpt-test-mini")
        self.assertEqual(request["text"]["format"]["type"], "json_schema")
        self.assertEqual(request["reasoning"]["effort"], "medium")
        self.assertEqual(request["temperature"], 0.1)
        self.assertEqual(request["max_output_tokens"], 777)

    def test_fallback_order_is_deterministic_when_structured_modes_fail(self) -> None:
        final = _FakeResponse('{"title":"x","summary":"y","sections":[{"category":"core","overview":"z","bullets":["a"]}]}')
        completions = _FallbackCompletions(final)
        sdk = type("SDK", (), {})()
        sdk.chat = _FakeChat(completions)
        sdk.models = type("FakeModels", (), {"list": lambda self: {"data": []}})()
        client = OpenAIProvider(sdk_client=sdk, model="gpt-test-mini")

        _ = client.generate_structured_json(
            system_prompt="sys",
            user_prompt="usr",
            json_schema={"type": "object"},
            structured_mode="strict_json_schema",
        )

        self.assertEqual(len(completions.calls), 3)
        self.assertEqual(completions.calls[0]["response_format"]["type"], "json_schema")
        self.assertEqual(completions.calls[1]["response_format"]["type"], "json_object")
        self.assertNotIn("response_format", completions.calls[2])
        self.assertEqual(client.last_structured_mode_used, "prompt_json")

    def test_non_recoverable_transport_error_does_not_fallback(self) -> None:
        sdk = _FakeSDKClient(_FakeResponse("{}"))
        sdk.chat.completions.error = RuntimeError("network timeout")
        client = OpenAIProvider(sdk_client=sdk, model="gpt-test-mini")

        with self.assertRaisesRegex(ValueError, "Chat Completions request failed"):
            client.generate_structured_json(
                system_prompt="sys",
                user_prompt="usr",
                json_schema={"type": "object"},
                structured_mode="strict_json_schema",
            )

        self.assertEqual(len(sdk.chat.completions.calls), 1)
        self.assertEqual(client.last_structured_mode_used, None)

    def test_json_wrapper_noise_is_recovered(self) -> None:
        sdk = _FakeSDKClient(
            _FakeResponse(
                'Result:\n```json\n{"title":"x","summary":"y","sections":[{"category":"core","overview":"z","bullets":["a"]}]}\n```'
            )
        )
        client = OpenAIProvider(sdk_client=sdk, model="gpt-test-mini")
        parsed = client.generate_structured_json(
            system_prompt="sys",
            user_prompt="usr",
            json_schema={"type": "object"},
        )
        self.assertEqual(parsed["title"], "x")

    def test_missing_api_key_fails_clearly(self) -> None:
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(ValueError, "OPENAI_API_KEY"):
                OpenAIProvider()

    def test_invalid_json_response_fails_clearly(self) -> None:
        sdk = _FakeSDKClient(_FakeResponse("not-json"))
        client = OpenAIProvider(sdk_client=sdk)

        with self.assertRaisesRegex(ValueError, "invalid JSON"):
            client.generate_structured_json(
                system_prompt="sys",
                user_prompt="usr",
                json_schema={"type": "object"},
            )

    def test_list_models_parses_ids(self) -> None:
        sdk = _FakeSDKClient(_FakeResponse("{}"))
        sdk.models = type(
            "FakeModels",
            (),
            {"list": lambda self: {"data": [{"id": "Qwen3.5-122B"}, {"id": "Gemma-4-31B"}]}},
        )()
        client = OpenAIProvider(sdk_client=sdk)

        self.assertEqual(client.list_models(), ["Gemma-4-31B", "Qwen3.5-122B"])

    def test_list_models_error_is_clear(self) -> None:
        class _BoomModels:
            def list(self):
                raise RuntimeError("Connection error")

        sdk = _FakeSDKClient(_FakeResponse("{}"))
        sdk.models = _BoomModels()
        client = OpenAIProvider(sdk_client=sdk)

        with self.assertRaisesRegex(ValueError, "Model discovery request failed"):
            client.list_models()


if __name__ == "__main__":
    unittest.main()
