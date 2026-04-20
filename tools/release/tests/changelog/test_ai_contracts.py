from __future__ import annotations

import json
from pathlib import Path
import unittest

from tools.release.changelog.ai.contracts.draft import (
    AIDraftResult,
    AI_DRAFT_RESPONSE_JSON_SCHEMA,
    ai_draft_result_to_dict,
    parse_ai_draft_response,
)


FIXTURES_DIR = Path(__file__).parent / "fixtures"


def _load_json_fixture(name: str) -> dict:
    return json.loads((FIXTURES_DIR / name).read_text(encoding="utf-8"))


class AIDraftContractsTests(unittest.TestCase):
    def test_schema_has_required_top_level_fields(self) -> None:
        self.assertEqual(AI_DRAFT_RESPONSE_JSON_SCHEMA["type"], "object")
        self.assertEqual(
            AI_DRAFT_RESPONSE_JSON_SCHEMA["required"],
            ["title", "summary", "sections"],
        )

    def test_parse_valid_response_fixture(self) -> None:
        raw = _load_json_fixture("ai_draft_valid.json")
        parsed = parse_ai_draft_response(raw)

        self.assertIsInstance(parsed, AIDraftResult)
        self.assertEqual(parsed.title, "Release 2.4.0 Draft")
        self.assertEqual(len(parsed.sections), 2)
        self.assertEqual(parsed.sections[0].category, "core")
        self.assertEqual(parsed.sections[0].bullets[0], raw["sections"][0]["bullets"][0])

    def test_parse_rejects_missing_sections(self) -> None:
        with self.assertRaisesRegex(ValueError, "sections"):
            parse_ai_draft_response({"title": "x", "summary": "y"})

    def test_parse_rejects_missing_title(self) -> None:
        with self.assertRaisesRegex(ValueError, "title"):
            parse_ai_draft_response({"summary": "x", "sections": [{"category": "core", "overview": "y", "bullets": ["z"]}]})

    def test_parse_rejects_invalid_bullets(self) -> None:
        invalid = {
            "title": "Release Draft",
            "summary": "Summary",
            "sections": [
                {
                    "category": "core",
                    "overview": "Overview",
                    "bullets": [""],
                }
            ],
        }
        with self.assertRaisesRegex(ValueError, "bullets"):
            parse_ai_draft_response(invalid)

    def test_result_to_dict_omits_empty_notes(self) -> None:
        parsed = parse_ai_draft_response(
            {
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
        rendered = ai_draft_result_to_dict(parsed)
        self.assertNotIn("notes", rendered)


if __name__ == "__main__":
    unittest.main()
