from __future__ import annotations

import json
from pathlib import Path
import unittest

from tools.release.changelog.ai.contracts.triage import (
    AITriageResult,
    AI_TRIAGE_RESPONSE_JSON_SCHEMA,
    AI_TRIAGE_SCHEMA_VERSION,
    ai_triage_result_to_dict,
    build_triage_ir_payload,
    parse_ai_triage_response,
)
from tools.release.changelog.ai.providers.parsing import parse_json_object_from_text


FIXTURES_DIR = Path(__file__).parent / "fixtures"


def _load_json_fixture(name: str) -> dict:
    return json.loads((FIXTURES_DIR / name).read_text(encoding="utf-8"))


class AITriageContractsTests(unittest.TestCase):
    maxDiff = None

    def test_schema_has_required_top_level_fields(self) -> None:
        self.assertEqual(AI_TRIAGE_RESPONSE_JSON_SCHEMA["type"], "object")
        self.assertEqual(
            AI_TRIAGE_RESPONSE_JSON_SCHEMA["required"],
            ["schema_version", "version", "summary_points", "categories"],
        )

    def test_parse_valid_response_fixture(self) -> None:
        raw = _load_json_fixture("ai_triage_valid.json")
        parsed = parse_ai_triage_response(raw)

        self.assertIsInstance(parsed, AITriageResult)
        self.assertEqual(parsed.schema_version, AI_TRIAGE_SCHEMA_VERSION)
        self.assertEqual(parsed.version, "2.4.0")
        self.assertEqual(parsed.categories[0].name, "core")
        self.assertEqual(parsed.categories[0].priority_rank, 1)
        self.assertEqual(parsed.categories[1].priority_rank, 2)

    def test_parse_rejects_invalid_signal_strength(self) -> None:
        invalid = _load_json_fixture("ai_triage_valid.json")
        invalid["categories"][0]["signal_strength"] = "high"

        with self.assertRaisesRegex(ValueError, "signal_strength"):
            parse_ai_triage_response(invalid)

    def test_parse_rejects_missing_schema_version(self) -> None:
        invalid = _load_json_fixture("ai_triage_valid.json")
        invalid.pop("schema_version", None)
        with self.assertRaisesRegex(ValueError, "schema_version"):
            parse_ai_triage_response(invalid)

    def test_optional_retained_snippets_drops_empty_entry(self) -> None:
        raw = _load_json_fixture("ai_triage_valid.json")
        raw["categories"][0]["retained_snippets"] = [""]
        parsed = parse_ai_triage_response(raw)
        self.assertEqual(parsed.categories[0].retained_snippets, ())

    def test_optional_retained_snippets_drops_whitespace_entry(self) -> None:
        raw = _load_json_fixture("ai_triage_valid.json")
        raw["categories"][0]["retained_snippets"] = ["   "]
        parsed = parse_ai_triage_response(raw)
        self.assertEqual(parsed.categories[0].retained_snippets, ())

    def test_optional_retained_snippets_preserves_valid_and_drops_blank(self) -> None:
        raw = _load_json_fixture("ai_triage_valid.json")
        raw["categories"][0]["retained_snippets"] = [" ", "kept snippet", ""]
        parsed = parse_ai_triage_response(raw)
        self.assertEqual(parsed.categories[0].retained_snippets, ("kept snippet",))

    def test_optional_retained_snippets_drops_null_placeholder(self) -> None:
        raw = _load_json_fixture("ai_triage_valid.json")
        raw["categories"][0]["retained_snippets"] = [None]
        parsed = parse_ai_triage_response(raw)
        self.assertEqual(parsed.categories[0].retained_snippets, ())

    def test_optional_retained_snippets_keeps_valid_and_drops_null(self) -> None:
        raw = _load_json_fixture("ai_triage_valid.json")
        raw["categories"][0]["retained_snippets"] = ["snippet", None]
        parsed = parse_ai_triage_response(raw)
        self.assertEqual(parsed.categories[0].retained_snippets, ("snippet",))

    def test_optional_retained_snippets_drops_non_string_placeholder(self) -> None:
        raw = _load_json_fixture("ai_triage_valid.json")
        raw["categories"][0]["retained_snippets"] = [1]
        parsed = parse_ai_triage_response(raw)
        self.assertEqual(parsed.categories[0].retained_snippets, ())

    def test_required_key_points_still_rejects_non_string(self) -> None:
        raw = _load_json_fixture("ai_triage_valid.json")
        raw["categories"][0]["key_points"] = [1]
        with self.assertRaisesRegex(ValueError, "key_points\\[0\\]"):
            parse_ai_triage_response(raw)

    def test_qwen_like_envelope_and_optional_array_noise_is_normalized(self) -> None:
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
                            "key_points": ["Service hardening work."],
                            "important_files": [" service.py ", None, ""],
                            "retained_snippets": ["", "   ", None, 1, "kept snippet"],
                            "caution_notes": [None, "weak signal", ""],
                        }
                    ],
                    "dropped_noise": [None, "", "minor refactors"],
                    "caution_notes": [None, "verify benchmarks"],
                }
            }
        )
        parsed_text = parse_json_object_from_text(content)
        parsed = parse_ai_triage_response(parsed_text)
        category = parsed.categories[0]
        self.assertEqual(category.retained_snippets, ("kept snippet",))
        self.assertEqual(category.important_files, ("service.py",))
        self.assertEqual(category.caution_notes, ("weak signal",))
        self.assertEqual(parsed.dropped_noise, ("minor refactors",))
        self.assertEqual(parsed.caution_notes, ("verify benchmarks",))

    def test_parse_rejects_duplicate_priority_rank(self) -> None:
        invalid = _load_json_fixture("ai_triage_valid.json")
        invalid["categories"][1]["priority_rank"] = 1

        with self.assertRaisesRegex(ValueError, "Duplicate triage priority rank"):
            parse_ai_triage_response(invalid)

    def test_result_to_dict_roundtrip_and_ir_payload(self) -> None:
        raw = _load_json_fixture("ai_triage_valid.json")
        parsed = parse_ai_triage_response(raw)
        as_dict = ai_triage_result_to_dict(parsed)
        triage_ir = build_triage_ir_payload(parsed)

        self.assertEqual(as_dict["schema_version"], AI_TRIAGE_SCHEMA_VERSION)
        self.assertEqual(triage_ir["schema_version"], AI_TRIAGE_SCHEMA_VERSION)
        self.assertEqual(triage_ir["categories"][0]["name"], "core")
        self.assertEqual(triage_ir["categories"][0]["priority_rank"], 1)


if __name__ == "__main__":
    unittest.main()
