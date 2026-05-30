from __future__ import annotations

import unittest

from tools.release.changelog.release_notes_merge import (
    GENERATED_RELEASE_NOTES_END,
    GENERATED_RELEASE_NOTES_START,
    merge_release_notes_body,
)


class ReleaseNotesMergeTests(unittest.TestCase):
    def test_existing_user_body_preserved_and_generated_section_appended(self) -> None:
        merged = merge_release_notes_body(
            "Operator overview.\n\nRollout note stays here.\n",
            "Short generated overview.\n\n## What's Changed\n\n- Fixed release tooling.\n",
            tag="v1.2.3",
        )

        self.assertTrue(merged.startswith("Operator overview.\n\nRollout note stays here."))
        self.assertIn(GENERATED_RELEASE_NOTES_START, merged)
        self.assertIn("## What's Changed\n\n- Fixed release tooling.", merged)
        self.assertNotIn("Short generated overview", merged)

    def test_existing_generated_marker_block_is_replaced(self) -> None:
        existing = (
            "Operator overview.\n\n"
            f"{GENERATED_RELEASE_NOTES_START}\n"
            "## What's Changed\n\n- Old generated item.\n"
            f"{GENERATED_RELEASE_NOTES_END}\n\n"
            "Footer note.\n"
        )

        merged = merge_release_notes_body(
            existing,
            "Overview.\n\n## What's Changed\n\n- New generated item.\n",
        )

        self.assertIn("Operator overview.", merged)
        self.assertIn("Footer note.", merged)
        self.assertIn("- New generated item.", merged)
        self.assertNotIn("- Old generated item.", merged)
        self.assertEqual(merged.count(GENERATED_RELEASE_NOTES_START), 1)
        self.assertEqual(merged.count(GENERATED_RELEASE_NOTES_END), 1)

    def test_empty_existing_body_uses_standalone_generated_notes(self) -> None:
        merged = merge_release_notes_body(
            "",
            "Generated overview.\n\n## What's Changed\n\n- Added packaging output.\n",
        )

        self.assertIn("Generated overview.", merged)
        self.assertIn("## What's Changed\n\n- Added packaging output.", merged)
        self.assertIn(GENERATED_RELEASE_NOTES_START, merged)

    def test_rerun_merge_is_idempotent(self) -> None:
        first = merge_release_notes_body(
            "Operator prose.\n",
            "Overview.\n\n## What's Changed\n\n- First generated item.\n",
        )
        second = merge_release_notes_body(
            first,
            "Overview.\n\n## What's Changed\n\n- First generated item.\n",
        )

        self.assertEqual(second, first)
        self.assertEqual(second.count("## What's Changed"), 1)
        self.assertEqual(second.count(GENERATED_RELEASE_NOTES_START), 1)

    def test_generated_notes_without_whats_changed_are_wrapped_for_append(self) -> None:
        merged = merge_release_notes_body("Operator prose.\n", "- Fixed context selection.\n")

        self.assertIn("Operator prose.", merged)
        self.assertIn("## What's Changed\n\n- Fixed context selection.", merged)

    def test_generated_notes_without_whats_changed_get_standalone_intro(self) -> None:
        merged = merge_release_notes_body("", "- Fixed context selection.\n")

        self.assertIn("Generated release notes for this Vaulthalla release.", merged)
        self.assertIn("## What's Changed\n\n- Fixed context selection.", merged)


if __name__ == "__main__":
    unittest.main()
