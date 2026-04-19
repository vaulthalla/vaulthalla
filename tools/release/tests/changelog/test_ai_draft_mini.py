from __future__ import annotations

import json
from pathlib import Path
import unittest

from tools.release.changelog.ai.draft_mini import (
    generate_mini_draft_from_payload,
    render_ai_draft_json,
    render_ai_draft_markdown,
)


FIXTURES_DIR = Path(__file__).parent / "fixtures"


def _load_json_fixture(name: str) -> dict:
    return json.loads((FIXTURES_DIR / name).read_text(encoding="utf-8"))


def _load_text_fixture(name: str) -> str:
    return (FIXTURES_DIR / name).read_text(encoding="utf-8")


class _FakeOpenAIClient:
    def __init__(self, response: dict) -> None:
        self._response = response
        self.calls: list[dict] = []

    def generate_structured_json(self, **kwargs):
        self.calls.append(kwargs)
        return self._response


class AIDraftMiniStageTests(unittest.TestCase):
    maxDiff = None

    def test_generate_mini_draft_uses_schema_and_payload_in_prompt(self) -> None:
        payload = {
            "schema_version": "vaulthalla.release.ai_payload.v1",
            "metadata": {"version": "2.4.0"},
            "categories": [],
        }
        fake = _FakeOpenAIClient(_load_json_fixture("ai_draft_valid.json"))

        draft = generate_mini_draft_from_payload(payload, client=fake)

        self.assertEqual(draft.title, "Release 2.4.0 Draft")
        self.assertEqual(len(fake.calls), 1)
        call = fake.calls[0]
        self.assertIn("json_schema", call)
        self.assertIn("schema_version", call["user_prompt"])
        self.assertIn("vaulthalla.release.ai_payload.v1", call["user_prompt"])

    def test_markdown_render_matches_fixture(self) -> None:
        draft = generate_mini_draft_from_payload(
            {"schema_version": "x", "metadata": {}, "categories": []},
            client=_FakeOpenAIClient(_load_json_fixture("ai_draft_valid.json")),
        )
        rendered = render_ai_draft_markdown(draft)
        self.assertEqual(rendered, _load_text_fixture("ai_draft_markdown.md"))

    def test_json_render_is_structured_and_stable(self) -> None:
        draft = generate_mini_draft_from_payload(
            {"schema_version": "x", "metadata": {}, "categories": []},
            client=_FakeOpenAIClient(_load_json_fixture("ai_draft_valid.json")),
        )
        first = render_ai_draft_json(draft)
        second = render_ai_draft_json(draft)
        self.assertEqual(first, second)
        self.assertIn('"sections": [', first)


if __name__ == "__main__":
    unittest.main()
