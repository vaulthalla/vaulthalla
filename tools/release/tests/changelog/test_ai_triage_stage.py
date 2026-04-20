from __future__ import annotations

import json
from pathlib import Path
import unittest

from tools.release.changelog.ai.stages.triage import render_triage_result_json, run_triage_stage


FIXTURES_DIR = Path(__file__).parent / "fixtures"


def _load_json_fixture(name: str) -> dict:
    return json.loads((FIXTURES_DIR / name).read_text(encoding="utf-8"))


class _FakeProvider:
    def __init__(self, response: dict) -> None:
        self._response = response
        self.calls: list[dict] = []

    def generate_structured_json(self, **kwargs):
        self.calls.append(kwargs)
        return self._response


class AITriageStageTests(unittest.TestCase):
    def test_run_triage_stage_uses_schema_and_payload_in_prompt(self) -> None:
        payload = {
            "schema_version": "vaulthalla.release.ai_payload.v1",
            "metadata": {"version": "2.4.0"},
            "categories": [],
        }
        fake = _FakeProvider(_load_json_fixture("ai_triage_valid.json"))

        triage = run_triage_stage(payload, provider=fake)

        self.assertEqual(triage.version, "2.4.0")
        self.assertEqual(len(fake.calls), 1)
        call = fake.calls[0]
        self.assertIn("json_schema", call)
        self.assertIn("Release payload", call["user_prompt"])
        self.assertIn("vaulthalla.release.ai_payload.v1", call["user_prompt"])

    def test_run_triage_stage_rejects_invalid_response(self) -> None:
        invalid = _load_json_fixture("ai_triage_valid.json")
        invalid["categories"] = []

        with self.assertRaisesRegex(ValueError, "categories"):
            run_triage_stage(
                {"schema_version": "x", "metadata": {}, "categories": []},
                provider=_FakeProvider(invalid),
            )

    def test_render_triage_json_is_stable(self) -> None:
        triage = run_triage_stage(
            {"schema_version": "x", "metadata": {}, "categories": []},
            provider=_FakeProvider(_load_json_fixture("ai_triage_valid.json")),
        )
        first = render_triage_result_json(triage)
        second = render_triage_result_json(triage)
        self.assertEqual(first, second)
        self.assertIn('"schema_version": "vaulthalla.release.ai_triage.v1"', first)


if __name__ == "__main__":
    unittest.main()
