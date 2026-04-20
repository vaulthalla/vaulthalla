from __future__ import annotations

import unittest
from unittest.mock import patch

from tools.release.changelog.git_collect import get_commits_since_tag


class GitCollectTests(unittest.TestCase):
    def test_empty_body_commit_is_not_dropped(self) -> None:
        git_log = "6cf9a156eae61fd9ba2c2204ee9c73c2832bd14b\x1fUpdate docs\x1f\x1e"

        with (
            patch("tools.release.changelog.git_collect._run_git", return_value=git_log),
            patch(
                "tools.release.changelog.git_collect.get_commit_file_summary",
                return_value=(["README.md"], 2, 1),
            ),
        ):
            commits = get_commits_since_tag(".", "v0.30.5")

        self.assertEqual(len(commits), 1)
        self.assertEqual(commits[0].sha, "6cf9a156eae61fd9ba2c2204ee9c73c2832bd14b")
        self.assertEqual(commits[0].subject, "Update docs")

    def test_metadata_only_packaging_release_commit_gets_non_meta_categories(self) -> None:
        git_log = (
            "6cf9a156eae61fd9ba2c2204ee9c73c2832bd14b\x1f"
            "update packaging documentation; clarify Debian build orchestration and release tooling structure"
            "\x1f\x1e"
        )

        with (
            patch("tools.release.changelog.git_collect._run_git", return_value=git_log),
            patch(
                "tools.release.changelog.git_collect.get_commit_file_summary",
                return_value=(["README.md"], 3, 0),
            ),
        ):
            commits = get_commits_since_tag(".", "v0.30.5")

        self.assertEqual(len(commits), 1)
        self.assertIn("meta", commits[0].categories)
        self.assertIn("debian", commits[0].categories)
        self.assertIn("tools", commits[0].categories)


if __name__ == "__main__":
    unittest.main()
