from __future__ import annotations

import unittest

from tools.release.changelog.ai.config import AIProviderConfig
from tools.release.changelog.ai.providers import run_provider_preflight


class _FakeDiscoveryProvider:
    def __init__(self, models: list[str]) -> None:
        self._models = models

    def list_models(self) -> list[str]:
        return list(self._models)


class _FailingDiscoveryProvider:
    def __init__(self, error: Exception) -> None:
        self._error = error

    def list_models(self) -> list[str]:
        raise self._error


class AIProviderPreflightTests(unittest.TestCase):
    def test_openai_compatible_preflight_success_with_model_match(self) -> None:
        config = AIProviderConfig(
            kind="openai-compatible",
            base_url="http://localhost:8888/v1",
            model="Qwen3.5-122B",
        )
        result = run_provider_preflight(
            config,
            provider=_FakeDiscoveryProvider(["Qwen3.5-122B", "Gemma-4-31B"]),
        )

        self.assertEqual(result.provider_kind, "openai-compatible")
        self.assertEqual(result.model, "Qwen3.5-122B")
        self.assertTrue(result.model_found)
        self.assertIn("Gemma-4-31B", result.discovered_models)

    def test_openai_compatible_preflight_surfaces_endpoint_failure(self) -> None:
        config = AIProviderConfig(
            kind="openai-compatible",
            base_url="http://localhost:8888/v1",
            model="Qwen3.5-122B",
        )

        with self.assertRaisesRegex(
            ValueError,
            "Could not reach OpenAI-compatible endpoint at http://localhost:8888/v1",
        ):
            run_provider_preflight(
                config,
                provider=_FailingDiscoveryProvider(RuntimeError("Connection error")),
            )

    def test_preflight_fails_when_model_missing_from_discovery(self) -> None:
        config = AIProviderConfig(
            kind="openai-compatible",
            base_url="http://localhost:8888/v1",
            model="Qwen3.5-122B",
        )

        with self.assertRaisesRegex(ValueError, "is not listed"):
            run_provider_preflight(
                config,
                provider=_FakeDiscoveryProvider(["Gemma-4-31B"]),
            )

    def test_openai_preflight_failure_has_hosted_context(self) -> None:
        config = AIProviderConfig(kind="openai", model="gpt-5.4-mini")

        with self.assertRaisesRegex(ValueError, "OpenAI provider preflight failed"):
            run_provider_preflight(
                config,
                provider=_FailingDiscoveryProvider(RuntimeError("Connection error")),
            )

    def test_preflight_rejects_empty_model_name(self) -> None:
        config = AIProviderConfig(kind="openai-compatible", base_url="http://localhost:8888/v1", model=" ")
        with self.assertRaisesRegex(ValueError, "--model"):
            run_provider_preflight(config, provider=_FakeDiscoveryProvider(["Qwen3.5-122B"]))


if __name__ == "__main__":
    unittest.main()
