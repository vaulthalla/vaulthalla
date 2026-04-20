from __future__ import annotations

import unittest

from tools.release.changelog.ai.providers import (
    get_provider_capabilities,
    resolve_generation_settings,
)


class AIProviderCapabilitiesTests(unittest.TestCase):
    def test_openai_capabilities(self) -> None:
        caps = get_provider_capabilities("openai")
        self.assertTrue(caps.supports_reasoning_effort)
        self.assertTrue(caps.supports_strict_schema)
        self.assertEqual(caps.default_structured_mode, "strict_json_schema")

    def test_openai_compatible_capabilities(self) -> None:
        caps = get_provider_capabilities("openai-compatible")
        self.assertFalse(caps.supports_reasoning_effort)
        self.assertFalse(caps.supports_strict_schema)
        self.assertEqual(caps.default_structured_mode, "json_object")

    def test_openai_preserves_requested_settings(self) -> None:
        resolved = resolve_generation_settings(
            provider_kind="openai",
            requested_structured_mode="strict_json_schema",
            requested_reasoning_effort="high",
        )
        self.assertEqual(resolved.structured_mode, "strict_json_schema")
        self.assertEqual(resolved.reasoning_effort, "high")
        self.assertEqual(resolved.degradations, ())

    def test_openai_compatible_degrades_strict_and_reasoning(self) -> None:
        resolved = resolve_generation_settings(
            provider_kind="openai-compatible",
            requested_structured_mode="strict_json_schema",
            requested_reasoning_effort="medium",
        )
        self.assertEqual(resolved.structured_mode, "json_object")
        self.assertIsNone(resolved.reasoning_effort)
        self.assertEqual(len(resolved.degradations), 2)

    def test_defaults_resolve_when_unset(self) -> None:
        hosted = resolve_generation_settings(provider_kind="openai")
        local = resolve_generation_settings(provider_kind="openai-compatible")
        self.assertEqual(hosted.structured_mode, "strict_json_schema")
        self.assertEqual(local.structured_mode, "json_object")
        self.assertIsNone(hosted.reasoning_effort)
        self.assertIsNone(local.reasoning_effort)

    def test_invalid_structured_mode_errors(self) -> None:
        with self.assertRaisesRegex(ValueError, "Invalid structured mode"):
            resolve_generation_settings(
                provider_kind="openai",
                requested_structured_mode="xml",  # type: ignore[arg-type]
            )

    def test_invalid_reasoning_effort_errors(self) -> None:
        with self.assertRaisesRegex(ValueError, "Invalid reasoning effort"):
            resolve_generation_settings(
                provider_kind="openai",
                requested_reasoning_effort="turbo",  # type: ignore[arg-type]
            )


if __name__ == "__main__":
    unittest.main()
