from __future__ import annotations

from pathlib import Path
import unittest


class ReleaseWorkflowContractTests(unittest.TestCase):
    def _workflow(self) -> str:
        repo_root = Path(__file__).resolve().parents[4]
        workflow_path = repo_root / ".github" / "workflows" / "release.yml"
        return workflow_path.read_text(encoding="utf-8")

    def test_github_release_assets_are_prepared_via_deduped_manifest_step(self) -> None:
        workflow = self._workflow()
        self.assertIn("Prepare GitHub release asset list (deduped)", workflow)
        self.assertIn("id: gh_release_assets", workflow)
        self.assertIn("find \"$artifact_dir\" -type f | LC_ALL=C sort -u", workflow)

    def test_github_release_action_uses_manifest_output_not_duplicate_globs(self) -> None:
        workflow = self._workflow()
        self.assertIn("files: ${{ steps.gh_release_assets.outputs.assets }}", workflow)
        self.assertIn("overwrite_files: true", workflow)
        self.assertIn("fail_on_unmatched_files: true", workflow)
        self.assertNotIn("release/**/**/*", workflow)
        self.assertNotIn("release/**/*", workflow)
        self.assertNotIn("release/*", workflow)


if __name__ == "__main__":
    unittest.main()
