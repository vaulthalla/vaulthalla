from __future__ import annotations

import unittest
from unittest.mock import patch

from tools.release.changelog.context_builder import build_release_context
from tools.release.changelog.models import CommitInfo


class ContextBuilderTests(unittest.TestCase):
    def test_same_version_tag_with_ahead_head_adds_warning(self) -> None:
        commits = [
            CommitInfo(
                sha="6cf9a156eae61fd9ba2c2204ee9c73c2832bd14b",
                subject="update packaging documentation",
                body="",
                files=["tools/release/changelog/render_raw.py"],
                insertions=5,
                deletions=1,
                categories=["tools"],
            )
        ]

        with (
            patch("tools.release.changelog.context_builder.get_latest_tag", return_value="v0.30.5"),
            patch(
                "tools.release.changelog.context_builder.get_head_sha",
                return_value="6cf9a156eae61fd9ba2c2204ee9c73c2832bd14b",
            ),
            patch("tools.release.changelog.context_builder.get_commits_since_tag", return_value=commits),
            patch(
                "tools.release.changelog.context_builder.get_release_file_stats",
                return_value={"tools/release/changelog/render_raw.py": (5, 1)},
            ),
            patch("tools.release.changelog.context_builder.extract_relevant_snippets", return_value={}),
        ):
            context = build_release_context(version="0.30.5", repo_root=".")

        self.assertEqual(context.commit_count, 1)
        self.assertEqual(context.previous_tag, "v0.30.5")
        self.assertTrue(context.cross_cutting_notes)
        self.assertIn("Release tag already exists for this version", context.cross_cutting_notes[0])


if __name__ == "__main__":
    unittest.main()
