from __future__ import annotations

import unittest

from tools.release.changelog.ai.prompts.draft import build_draft_system_prompt, build_draft_user_prompt
from tools.release.changelog.ai.prompts.polish import build_polish_system_prompt, build_polish_user_prompt
from tools.release.changelog.ai.prompts.release_notes import (
    build_release_notes_system_prompt,
    build_release_notes_user_prompt,
)
from tools.release.changelog.ai.prompts.triage import build_triage_system_prompt, build_triage_user_prompt


class AIPromptDisciplineTests(unittest.TestCase):
    def test_triage_prompt_enforces_no_speculative_language(self) -> None:
        system = build_triage_system_prompt().lower()
        self.assertIn("deterministic", system)
        self.assertIn("do not use speculative wording", system)
        self.assertIn("return json only", system)

    def test_draft_prompt_enforces_evidence_bound_output(self) -> None:
        system = build_draft_system_prompt().lower()
        user = build_draft_user_prompt({"schema_version": "x", "categories": []}).lower()
        self.assertIn("use only explicit evidence", system)
        self.assertIn("do not use speculative wording", system)
        self.assertIn("directly attributable to input evidence", user)
        self.assertIn("foreground the 2-3 strongest release themes", user)
        self.assertIn("do not give weak/low-signal categories equal space", user)
        self.assertIn("synthesize related evidence", user)
        self.assertIn("do not add intro/outro filler", user)
        self.assertIn("required top-level output fields", user)
        self.assertIn("`title`, `summary`, `sections`", user)

    def test_polish_prompt_constrains_semantic_changes(self) -> None:
        system = build_polish_system_prompt().lower()
        user = build_polish_user_prompt({"title": "t", "summary": "s", "sections": []}).lower()
        self.assertIn("wording refinement only", system)
        self.assertIn("never add meaning", system)
        self.assertIn("merge repetitive phrasing", user)
        self.assertIn("remove redundancy between section overviews and bullets", user)
        self.assertIn("increasing verbosity", user)
        self.assertIn("return json only", user)
        self.assertIn("schema_version", user)
        self.assertIn("required top-level output fields", user)

    def test_triage_prompt_requires_schema_version_contract_fields(self) -> None:
        user = build_triage_user_prompt({"schema_version": "x"}).lower()
        self.assertIn("required top-level output fields", user)
        self.assertIn("schema_version", user)
        self.assertIn("never emit empty placeholders in arrays", user)
        self.assertIn("nulls", user)
        self.assertIn("non-string placeholders", user)

    def test_required_input_labels_preserved(self) -> None:
        triage_user = build_triage_user_prompt({"schema_version": "x"})
        draft_user = build_draft_user_prompt({"schema_version": "x"}, source_kind="triage")
        polish_user = build_polish_user_prompt({"title": "t"})
        self.assertIn("Release payload", triage_user)
        self.assertIn("Triage IR", draft_user)
        self.assertIn("Draft input", polish_user)

    def test_hosted_compact_triage_prompt_enforces_compression(self) -> None:
        system = build_triage_system_prompt(hosted_compact_mode=True).lower()
        user = build_triage_user_prompt({"schema_version": "x"}, hosted_compact_mode=True).lower()
        self.assertIn("compression stage", system)
        self.assertIn("top-category distinctions", system)
        self.assertIn("compression mode (hosted)", user)
        self.assertIn("top category distinctions", user)
        self.assertIn("retained_snippets", user)
        self.assertIn("no rationale prose", user)

    def test_release_notes_prompt_enforces_no_invention(self) -> None:
        system = build_release_notes_system_prompt().lower()
        user = build_release_notes_user_prompt("# Changelog\n- fix\n").lower()
        self.assertIn("do not invent", system)
        self.assertIn("do not contradict", system)
        self.assertIn("do not write marketing copy", system)
        self.assertIn("preserve cautions", system)
        self.assertIn("schema_version", user)
        self.assertIn("return markdown in `markdown`", user)


if __name__ == "__main__":
    unittest.main()
