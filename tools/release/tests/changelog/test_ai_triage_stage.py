from __future__ import annotations

import json
from pathlib import Path
import unittest

from tools.release.changelog.ai.contracts.triage import AI_TRIAGE_SCHEMA_VERSION
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

    def test_run_triage_stage_passes_reasoning_and_structured_mode(self) -> None:
        payload = {
            "schema_version": "vaulthalla.release.ai_payload.v1",
            "metadata": {"version": "2.4.0"},
            "categories": [],
        }
        fake = _FakeProvider(_load_json_fixture("ai_triage_valid.json"))

        _ = run_triage_stage(
            payload,
            provider=fake,
            reasoning_effort="low",
            structured_mode="prompt_json",
            temperature=0.0,
            max_output_tokens_policy=123,
        )
        call = fake.calls[0]
        self.assertEqual(call["reasoning_effort"], "low")
        self.assertEqual(call["structured_mode"], "prompt_json")
        self.assertEqual(call["temperature"], 0.0)
        self.assertEqual(call["max_output_tokens"], 123)

    def test_render_triage_json_is_stable(self) -> None:
        triage = run_triage_stage(
            {"schema_version": "x", "metadata": {}, "categories": []},
            provider=_FakeProvider(_load_json_fixture("ai_triage_valid.json")),
        )
        first = render_triage_result_json(triage)
        second = render_triage_result_json(triage)
        self.assertEqual(first, second)
        self.assertIn('"schema_version": "vaulthalla.release.ai_triage.v1"', first)

    def test_run_triage_stage_normalizes_qwen_like_optional_array_noise(self) -> None:
        payload = {
            "schema_version": "vaulthalla.release.ai_payload.v1",
            "metadata": {"version": "2.4.0"},
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
                    "key_points": ["Service hardening work."],
                    "important_files": [" service.py ", None, ""],
                    "retained_snippets": ["", "   ", None, 1, "kept snippet"],
                    "caution_notes": [None, "weak signal", ""],
                }
            ],
            "dropped_noise": [None, "", "minor refactors"],
            "caution_notes": [None, "verify benchmarks"],
        }
        triage = run_triage_stage(payload, provider=_FakeProvider(response))
        category = triage.categories[0]
        self.assertEqual(category.important_files, ("service.py",))
        self.assertEqual(category.retained_snippets, ("kept snippet",))
        self.assertEqual(category.caution_notes, ("weak signal",))
        self.assertEqual(triage.dropped_noise, ("minor refactors",))
        self.assertEqual(triage.caution_notes, ("verify benchmarks",))


if __name__ == "__main__":
    unittest.main()
