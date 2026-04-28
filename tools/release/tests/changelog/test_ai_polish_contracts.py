from __future__ import annotations

import unittest

from tools.release.changelog.ai.contracts.polish import (
    AIPolishResult,
    AI_POLISH_RESPONSE_JSON_SCHEMA,
    AI_POLISH_SCHEMA_VERSION,
    ai_polish_result_to_dict,
    parse_ai_polish_response,
)
from tools.release.tests.changelog._support import load_json_fixture


class AIPolishContractsTests(unittest.TestCase):
    def test_schema_has_required_top_level_fields(self) -> None:
        self.assertEqual(AI_POLISH_RESPONSE_JSON_SCHEMA["type"], "object")
        self.assertEqual(
            AI_POLISH_RESPONSE_JSON_SCHEMA["required"],
            ["schema_version", "title", "summary", "sections", "notes"],
        )

    def test_parse_valid_response_fixture(self) -> None:
        raw = load_json_fixture(__file__, "ai_polish_valid.json")
        parsed = parse_ai_polish_response(raw)

        self.assertIsInstance(parsed, AIPolishResult)
        self.assertEqual(parsed.schema_version, AI_POLISH_SCHEMA_VERSION)
        self.assertEqual(parsed.title, "Release 2.4.0 Draft")
        self.assertEqual(parsed.sections[0].category, "core")
        self.assertEqual(parsed.sections[0].bullets[0], raw["sections"][0]["bullets"][0])

    def test_parse_rejects_missing_sections(self) -> None:
        invalid = {
            "schema_version": AI_POLISH_SCHEMA_VERSION,
            "title": "x",
            "summary": "y",
        }
        with self.assertRaisesRegex(ValueError, "sections"):
            parse_ai_polish_response(invalid)

    def test_parse_rejects_missing_schema_version(self) -> None:
        invalid = load_json_fixture(__file__, "ai_polish_valid.json")
        invalid.pop("schema_version", None)
        with self.assertRaisesRegex(ValueError, "schema_version"):
            parse_ai_polish_response(invalid)

    def test_parse_rejects_empty_bullet(self) -> None:
        invalid = load_json_fixture(__file__, "ai_polish_valid.json")
        invalid["sections"][0]["bullets"] = [""]

        with self.assertRaisesRegex(ValueError, "bullets"):
            parse_ai_polish_response(invalid)

    def test_result_to_dict_omits_empty_notes(self) -> None:
        parsed = parse_ai_polish_response(
            {
                "schema_version": AI_POLISH_SCHEMA_VERSION,
                "title": "Release Draft",
                "summary": "Summary",
                "sections": [
                    {
                        "category": "core",
                        "overview": "Overview",
                        "bullets": ["A"],
                    }
                ],
            }
        )
        rendered = ai_polish_result_to_dict(parsed)
        self.assertNotIn("notes", rendered)


if __name__ == "__main__":
    unittest.main()
