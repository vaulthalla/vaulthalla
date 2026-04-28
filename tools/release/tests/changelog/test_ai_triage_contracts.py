from __future__ import annotations

import json
import unittest

from tools.release.changelog.ai.contracts.triage import (
    AI_TRIAGE_HOSTED_COMPACT_RESPONSE_JSON_SCHEMA,
    AITriageResult,
    AI_TRIAGE_RESPONSE_JSON_SCHEMA,
    AI_TRIAGE_SCHEMA_VERSION,
    ai_triage_result_to_dict,
    build_triage_ir_payload,
    parse_ai_triage_response,
    resolve_triage_response_json_schema,
)
from tools.release.changelog.ai.providers.parsing import parse_json_object_from_text
from tools.release.tests.changelog._support import load_json_fixture


class AITriageContractsTests(unittest.TestCase):
    maxDiff = None

    def test_schema_has_required_top_level_fields(self) -> None:
        self.assertEqual(AI_TRIAGE_RESPONSE_JSON_SCHEMA["type"], "object")
        self.assertEqual(
            AI_TRIAGE_RESPONSE_JSON_SCHEMA["required"],
            [
                "schema_version",
                "version",
                "categories",
            ],
        )

    def test_hosted_compact_schema_is_smaller_for_triage(self) -> None:
        default_categories_max = AI_TRIAGE_RESPONSE_JSON_SCHEMA["properties"]["categories"]["maxItems"]
        hosted_categories_max = AI_TRIAGE_HOSTED_COMPACT_RESPONSE_JSON_SCHEMA["properties"]["categories"]["maxItems"]
        default_summary_max = AI_TRIAGE_RESPONSE_JSON_SCHEMA["properties"]["summary_points"]["maxItems"]
        hosted_summary_max = AI_TRIAGE_HOSTED_COMPACT_RESPONSE_JSON_SCHEMA["properties"]["summary_points"]["maxItems"]
        self.assertLess(hosted_categories_max, default_categories_max)
        self.assertLess(hosted_summary_max, default_summary_max)

    def test_schema_resolver_uses_hosted_compact_for_hosted_gpt5(self) -> None:
        resolved = resolve_triage_response_json_schema(provider_kind="openai", model="gpt-5-nano")
        self.assertEqual(
            resolved["properties"]["categories"]["maxItems"],
            AI_TRIAGE_HOSTED_COMPACT_RESPONSE_JSON_SCHEMA["properties"]["categories"]["maxItems"],
        )

    def test_schema_resolver_keeps_default_for_local_compatible(self) -> None:
        resolved = resolve_triage_response_json_schema(
            provider_kind="openai-compatible",
            model="gpt-5-nano",
        )
        self.assertEqual(
            resolved["properties"]["categories"]["maxItems"],
            AI_TRIAGE_RESPONSE_JSON_SCHEMA["properties"]["categories"]["maxItems"],
        )

    def test_parse_valid_response_fixture(self) -> None:
        raw = load_json_fixture(__file__, "ai_triage_valid.json")
        parsed = parse_ai_triage_response(raw)

        self.assertIsInstance(parsed, AITriageResult)
        self.assertEqual(parsed.schema_version, AI_TRIAGE_SCHEMA_VERSION)
        self.assertEqual(parsed.version, "2.4.0")
        self.assertEqual(parsed.categories[0].name, "core")
        self.assertEqual(parsed.categories[0].priority_rank, 1)
        self.assertEqual(parsed.categories[0].theme, "Core reliability and lifecycle hardening")
        self.assertEqual(parsed.categories[1].priority_rank, 2)

    def test_parse_rejects_invalid_signal_strength(self) -> None:
        invalid = load_json_fixture(__file__, "ai_triage_valid.json")
        invalid["categories"][0]["signal_strength"] = "high"

        with self.assertRaisesRegex(ValueError, "signal_strength"):
            parse_ai_triage_response(invalid)

    def test_parse_rejects_missing_schema_version(self) -> None:
        invalid = load_json_fixture(__file__, "ai_triage_valid.json")
        invalid.pop("schema_version", None)
        with self.assertRaisesRegex(ValueError, "schema_version"):
            parse_ai_triage_response(invalid)

    def test_optional_evidence_refs_drops_empty_like_entries(self) -> None:
        raw = load_json_fixture(__file__, "ai_triage_valid.json")
        raw["categories"][0]["evidence_refs"] = ["", "  ", None, 1, "core/path#kind"]
        parsed = parse_ai_triage_response(raw)
        self.assertEqual(parsed.categories[0].evidence_refs, ("core/path#kind",))

    def test_parse_allows_missing_summary_points(self) -> None:
        raw = load_json_fixture(__file__, "ai_triage_valid.json")
        raw.pop("summary_points", None)
        parsed = parse_ai_triage_response(raw)
        self.assertEqual(parsed.summary_points, ())

    def test_required_grounded_claims_still_rejects_non_string(self) -> None:
        raw = load_json_fixture(__file__, "ai_triage_valid.json")
        raw["categories"][0]["grounded_claims"] = [1]
        with self.assertRaisesRegex(ValueError, "grounded_claims\\[0\\]"):
            parse_ai_triage_response(raw)

    def test_qwen_like_optional_noise_is_normalized(self) -> None:
        content = json.dumps(
            {
                "result": {
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
            }
        )
        parsed_text = parse_json_object_from_text(content)
        parsed = parse_ai_triage_response(parsed_text)
        category = parsed.categories[0]
        self.assertEqual(category.evidence_refs, ("service.py#error-handling",))
        self.assertIsNone(category.operator_note)
        self.assertIsNone(parsed.operator_note)

    def test_parse_rejects_duplicate_priority_rank(self) -> None:
        invalid = load_json_fixture(__file__, "ai_triage_valid.json")
        invalid["categories"][1]["priority_rank"] = 1

        with self.assertRaisesRegex(ValueError, "Duplicate triage priority rank"):
            parse_ai_triage_response(invalid)

    def test_result_to_dict_roundtrip_and_ir_payload(self) -> None:
        raw = load_json_fixture(__file__, "ai_triage_valid.json")
        parsed = parse_ai_triage_response(raw)
        as_dict = ai_triage_result_to_dict(parsed)
        triage_ir = build_triage_ir_payload(parsed)

        self.assertEqual(as_dict["schema_version"], AI_TRIAGE_SCHEMA_VERSION)
        self.assertEqual(triage_ir["schema_version"], AI_TRIAGE_SCHEMA_VERSION)
        self.assertEqual(triage_ir["categories"][0]["name"], "core")
        self.assertEqual(triage_ir["categories"][0]["priority_rank"], 1)
        self.assertIn("theme", triage_ir["categories"][0])
        self.assertIn("grounded_claims", triage_ir["categories"][0])
        self.assertNotIn("important_files", triage_ir["categories"][0])
        self.assertNotIn("retained_snippets", triage_ir["categories"][0])


if __name__ == "__main__":
    unittest.main()
