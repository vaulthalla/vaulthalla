from __future__ import annotations

import unittest

from tools.release.changelog.ai.contracts.draft import parse_ai_draft_response
from tools.release.changelog.ai.render.markdown import render_polish_markdown
from tools.release.changelog.ai.stages.polish import render_polish_result_json, run_polish_stage
from tools.release.tests.changelog._support import (
    RecordingStructuredProvider,
    load_json_fixture,
    load_text_fixture,
)


class AIPolishStageTests(unittest.TestCase):
    maxDiff = None

    def _draft_input(self):
        return parse_ai_draft_response(load_json_fixture(__file__, "ai_draft_valid.json"))

    def test_run_polish_stage_uses_schema_and_draft_in_prompt(self) -> None:
        draft = self._draft_input()
        fake = RecordingStructuredProvider(load_json_fixture(__file__, "ai_polish_valid.json"))

        polish = run_polish_stage(draft, provider=fake)

        self.assertEqual(polish.schema_version, "vaulthalla.release.ai_polish.v1")
        self.assertEqual(len(fake.calls), 1)
        call = fake.calls[0]
        self.assertEqual(call["stage"], "polish")
        self.assertIn("json_schema", call)
        self.assertIn("Draft input", call["user_prompt"])
        self.assertIn("Release 2.4.0 Draft", call["user_prompt"])

    def test_run_polish_stage_rejects_invalid_response(self) -> None:
        draft = self._draft_input()
        invalid = load_json_fixture(__file__, "ai_polish_valid.json")
        invalid["sections"] = []

        with self.assertRaisesRegex(ValueError, "sections"):
            run_polish_stage(draft, provider=RecordingStructuredProvider(invalid))

    def test_run_polish_stage_passes_reasoning_and_structured_mode(self) -> None:
        draft = self._draft_input()
        fake = RecordingStructuredProvider(load_json_fixture(__file__, "ai_polish_valid.json"))

        _ = run_polish_stage(
            draft,
            provider=fake,
            reasoning_effort="high",
            structured_mode="json_object",
            temperature=0.0,
            max_output_tokens_policy=789,
        )
        call = fake.calls[0]
        self.assertEqual(call["stage"], "polish")
        self.assertEqual(call["reasoning_effort"], "high")
        self.assertEqual(call["structured_mode"], "json_object")
        self.assertEqual(call["temperature"], 0.0)
        self.assertEqual(call["max_output_tokens"], 789)

    def test_polish_markdown_render_matches_fixture(self) -> None:
        draft = self._draft_input()
        polish = run_polish_stage(
            draft,
            provider=RecordingStructuredProvider(load_json_fixture(__file__, "ai_polish_valid.json")),
        )
        rendered = render_polish_markdown(polish)
        self.assertEqual(rendered, load_text_fixture(__file__, "ai_polish_markdown.md"))

    def test_render_polish_json_is_stable(self) -> None:
        draft = self._draft_input()
        polish = run_polish_stage(
            draft,
            provider=RecordingStructuredProvider(load_json_fixture(__file__, "ai_polish_valid.json")),
        )

        first = render_polish_result_json(polish)
        second = render_polish_result_json(polish)
        self.assertEqual(first, second)
        self.assertIn('"schema_version": "vaulthalla.release.ai_polish.v1"', first)


if __name__ == "__main__":
    unittest.main()
