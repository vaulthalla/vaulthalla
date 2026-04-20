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
