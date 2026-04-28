from __future__ import annotations

import unittest

from tools.release.changelog.ai.render.markdown import render_draft_markdown
from tools.release.changelog.ai.stages.draft import generate_draft_from_payload, render_draft_result_json
from tools.release.tests.changelog._support import (
    RecordingStructuredProvider,
    load_json_fixture,
    load_text_fixture,
)


class AIDraftMiniStageTests(unittest.TestCase):
    maxDiff = None

    def test_generate_mini_draft_uses_schema_and_payload_in_prompt(self) -> None:
        payload = {
            "schema_version": "vaulthalla.release.ai_payload.v1",
            "metadata": {"version": "2.4.0"},
            "categories": [],
        }
        fake = RecordingStructuredProvider(load_json_fixture(__file__, "ai_draft_valid.json"))

        draft = generate_draft_from_payload(payload, provider=fake)

        self.assertEqual(draft.title, "Release 2.4.0 Draft")
        self.assertEqual(len(fake.calls), 1)
        call = fake.calls[0]
        self.assertEqual(call["stage"], "draft")
        self.assertIn("json_schema", call)
        self.assertIn("schema_version", call["user_prompt"])
        self.assertIn("vaulthalla.release.ai_payload.v1", call["user_prompt"])
        self.assertIn("Release payload", call["user_prompt"])

    def test_generate_draft_can_use_triage_input_label(self) -> None:
        triage_ir = {
            "schema_version": "vaulthalla.release.ai_triage.v2",
            "version": "2.4.0",
            "summary_points": ["Core work dominates."],
            "categories": [],
        }
        fake = RecordingStructuredProvider(load_json_fixture(__file__, "ai_draft_valid.json"))

        _ = generate_draft_from_payload(triage_ir, provider=fake, source_kind="triage")
        call = fake.calls[0]
        self.assertEqual(call["stage"], "draft")
        self.assertIn("Triage IR", call["user_prompt"])

    def test_generate_draft_passes_reasoning_and_structured_mode(self) -> None:
        payload = {"schema_version": "x", "metadata": {}, "categories": []}
        fake = RecordingStructuredProvider(load_json_fixture(__file__, "ai_draft_valid.json"))

        _ = generate_draft_from_payload(
            payload,
            provider=fake,
            reasoning_effort="medium",
            structured_mode="json_object",
            temperature=0.3,
            max_output_tokens_policy=456,
        )
        call = fake.calls[0]
        self.assertEqual(call["stage"], "draft")
        self.assertEqual(call["reasoning_effort"], "medium")
        self.assertEqual(call["structured_mode"], "json_object")
        self.assertEqual(call["temperature"], 0.3)
        self.assertEqual(call["max_output_tokens"], 456)

    def test_markdown_render_matches_fixture(self) -> None:
        draft = generate_draft_from_payload(
            {"schema_version": "x", "metadata": {}, "categories": []},
            provider=RecordingStructuredProvider(load_json_fixture(__file__, "ai_draft_valid.json")),
        )
        rendered = render_draft_markdown(draft)
        self.assertEqual(rendered, load_text_fixture(__file__, "ai_draft_markdown.md"))

    def test_json_render_is_structured_and_stable(self) -> None:
        draft = generate_draft_from_payload(
            {"schema_version": "x", "metadata": {}, "categories": []},
            provider=RecordingStructuredProvider(load_json_fixture(__file__, "ai_draft_valid.json")),
        )
        first = render_draft_result_json(draft)
        second = render_draft_result_json(draft)
        self.assertEqual(first, second)
        self.assertIn('"sections": [', first)


if __name__ == "__main__":
    unittest.main()
