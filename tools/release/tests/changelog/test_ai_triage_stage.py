from __future__ import annotations

import json
import unittest

from tools.release.changelog.ai.contracts.triage import AI_TRIAGE_SCHEMA_VERSION
from tools.release.changelog.ai.stages.triage import render_triage_result_json, run_triage_stage
from tools.release.tests.changelog._support import RecordingStructuredProvider, load_json_fixture


class AITriageStageTests(unittest.TestCase):
    def test_run_triage_stage_uses_schema_and_payload_in_prompt(self) -> None:
        payload = {
            "schema_version": "vaulthalla.release.semantic_payload.v1",
            "version": "2.4.0",
            "categories": [],
        }
        fake = RecordingStructuredProvider(load_json_fixture(__file__, "ai_triage_valid.json"))

        triage = run_triage_stage(payload, provider=fake)

        self.assertEqual(triage.version, "2.4.0")
        self.assertEqual(len(fake.calls), 1)
        call = fake.calls[0]
        self.assertEqual(call["stage"], "triage")
        self.assertIn("json_schema", call)
        self.assertIn("Semantic payload (compact projection)", call["user_prompt"])
        self.assertIn("vaulthalla.release.semantic_payload.v1", call["user_prompt"])

    def test_run_triage_stage_rejects_invalid_response(self) -> None:
        invalid = load_json_fixture(__file__, "ai_triage_valid.json")
        invalid["categories"] = []

        with self.assertRaisesRegex(ValueError, "categories"):
            run_triage_stage(
                {"schema_version": "x", "metadata": {}, "categories": []},
                provider=RecordingStructuredProvider(invalid),
            )

    def test_run_triage_stage_passes_reasoning_and_structured_mode(self) -> None:
        payload = {
            "schema_version": "vaulthalla.release.semantic_payload.v1",
            "version": "2.4.0",
            "categories": [],
        }
        fake = RecordingStructuredProvider(load_json_fixture(__file__, "ai_triage_valid.json"))

        _ = run_triage_stage(
            payload,
            provider=fake,
            reasoning_effort="low",
            structured_mode="prompt_json",
            temperature=0.0,
            max_output_tokens_policy=123,
        )
        call = fake.calls[0]
        self.assertEqual(call["stage"], "triage")
        self.assertEqual(call["reasoning_effort"], "low")
        self.assertEqual(call["structured_mode"], "prompt_json")
        self.assertEqual(call["temperature"], 0.0)
        self.assertEqual(call["max_output_tokens"], 123)

    def test_run_triage_stage_hosted_gpt5_uses_compact_schema_and_prompt(self) -> None:
        payload = {
            "schema_version": "vaulthalla.release.semantic_payload.v1",
            "version": "2.4.0",
            "categories": [],
        }
        fake = RecordingStructuredProvider(load_json_fixture(__file__, "ai_triage_valid.json"))

        _ = run_triage_stage(
            payload,
            provider=fake,
            provider_kind="openai",
            model="gpt-5-nano",
        )
        call = fake.calls[0]
        self.assertEqual(call["json_schema"]["properties"]["categories"]["maxItems"], 5)
        self.assertIn("Compression mode (hosted)", call["user_prompt"])

    def test_run_triage_stage_local_provider_keeps_default_schema_limits(self) -> None:
        payload = {
            "schema_version": "vaulthalla.release.semantic_payload.v1",
            "version": "2.4.0",
            "categories": [],
        }
        fake = RecordingStructuredProvider(load_json_fixture(__file__, "ai_triage_valid.json"))

        _ = run_triage_stage(
            payload,
            provider=fake,
            provider_kind="openai-compatible",
            model="gpt-5-nano",
        )
        call = fake.calls[0]
        self.assertEqual(call["json_schema"]["properties"]["categories"]["maxItems"], 10)
        self.assertNotIn("Compression mode (hosted)", call["user_prompt"])

    def test_render_triage_json_is_stable(self) -> None:
        triage = run_triage_stage(
            {"schema_version": "x", "metadata": {}, "categories": []},
            provider=RecordingStructuredProvider(load_json_fixture(__file__, "ai_triage_valid.json")),
        )
        first = render_triage_result_json(triage)
        second = render_triage_result_json(triage)
        self.assertEqual(first, second)
        self.assertIn('"schema_version": "vaulthalla.release.ai_triage.v2"', first)

    def test_run_triage_stage_normalizes_qwen_like_optional_array_noise(self) -> None:
        payload = {
            "schema_version": "vaulthalla.release.semantic_payload.v1",
            "version": "2.4.0",
            "categories": [],
        }
        response = {
            "schema_version": AI_TRIAGE_SCHEMA_VERSION,
            "version": "2.4.0",
            "summary_points": ["Core work dominates."],
            "categories": [
                {
                    "name": "core",
                    "signal_strength": "strong",
                    "priority_rank": 1,
                    "theme": "Core runtime hardening",
                    "grounded_claims": ["Service hardening work."],
                    "evidence_refs": [" service.py#error-handling ", None, ""],
                    "operator_note": 1,
                }
            ],
            "operator_note": None,
        }
        triage = run_triage_stage(payload, provider=RecordingStructuredProvider(response))
        category = triage.categories[0]
        self.assertEqual(category.evidence_refs, ("service.py#error-handling",))
        self.assertIsNone(category.operator_note)
        self.assertIsNone(triage.operator_note)

    def test_run_triage_stage_allows_missing_summary_points(self) -> None:
        payload = {
            "schema_version": "vaulthalla.release.semantic_payload.v1",
            "version": "2.4.0",
            "categories": [],
        }
        response = load_json_fixture(__file__, "ai_triage_valid.json")
        response.pop("summary_points", None)

        triage = run_triage_stage(payload, provider=RecordingStructuredProvider(response))
        self.assertEqual(triage.summary_points, ())

    def test_run_triage_stage_compact_projection_accepts_tuple_payload_arrays(self) -> None:
        payload = {
            "schema_version": "vaulthalla.release.semantic_payload.v1",
            "version": "2.4.0",
            "categories": (
                {
                    "name": "tools",
                    "signal_strength": "strong",
                    "summary_hint": "Release tooling contract updates",
                    "key_commits": ("Switch triage to semantic payload",),
                    "semantic_hunks": (
                        {
                            "kind": "prompt-contract",
                            "why_selected": "changed triage prompt contract text",
                            "excerpt": "@@ -1,2 +1,3 @@\n+required categories",
                            "path": "tools/release/changelog/ai/prompts/triage.py",
                        },
                    ),
                    "supporting_files": ("tools/release/changelog/ai/prompts/triage.py",),
                },
            ),
            "notes": ("note a",),
        }
        fake = RecordingStructuredProvider(load_json_fixture(__file__, "ai_triage_valid.json"))

        _ = run_triage_stage(payload, provider=fake)
        call = fake.calls[0]
        marker = "Semantic payload (compact projection):\n"
        projection_text = call["user_prompt"].split(marker, 1)[1]
        projection = json.loads(projection_text)

        self.assertEqual(len(projection["categories"]), 1)
        tools = projection["categories"][0]
        self.assertEqual(tools["name"], "tools")
        self.assertEqual(tools["key_commits"], ["Switch triage to semantic payload"])
        self.assertEqual(len(tools["semantic_hunks"]), 1)
        self.assertEqual(tools["semantic_hunks"][0]["kind"], "prompt-contract")
        self.assertEqual(tools["supporting_files"], ["tools/release/changelog/ai/prompts/triage.py"])

    def test_run_triage_stage_supports_synthesized_input_mode(self) -> None:
        payload = {
            "schema_version": "vaulthalla.release.triage_input.synthesized.v1",
            "version": "2.4.0",
            "categories": [
                {
                    "name": "tools",
                    "signal_strength": "strong",
                    "summary_hint": "Release tooling changes",
                    "key_commits": ["Improve ai-compare output"],
                    "synthesized_units": [
                        {
                            "id": "tools:1",
                            "change_kind": "output-artifact",
                            "change_summary": "Added profile comparison artifact generation.",
                            "confidence": "high",
                            "source_path": "tools/release/cli.py",
                            "source_kind": "output-artifact",
                        }
                    ],
                }
            ],
        }
        fake = RecordingStructuredProvider(load_json_fixture(__file__, "ai_triage_valid.json"))

        _ = run_triage_stage(payload, provider=fake, input_mode="synthesized_semantic")
        call = fake.calls[0]
        marker = "Synthesized semantic payload (compact projection):\n"
        projection_text = call["user_prompt"].split(marker, 1)[1]
        projection = json.loads(projection_text)

        self.assertIn("pre-synthesized unit summaries", call["system_prompt"])
        self.assertEqual(len(projection["categories"]), 1)
        units = projection["categories"][0]["synthesized_units"]
        self.assertEqual(units[0]["id"], "tools:1")
        self.assertEqual(units[0]["change_kind"], "output-artifact")


if __name__ == "__main__":
    unittest.main()
