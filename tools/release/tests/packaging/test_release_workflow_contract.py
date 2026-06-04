from __future__ import annotations

from pathlib import Path
import unittest


class ReleaseWorkflowContractTests(unittest.TestCase):
    def _repo_root(self) -> Path:
        return Path(__file__).resolve().parents[4]

    def _workflow(self) -> str:
        repo_root = self._repo_root()
        workflow_path = repo_root / ".github" / "workflows" / "release.yml"
        return workflow_path.read_text(encoding="utf-8")

    def _package_action(self) -> str:
        repo_root = self._repo_root()
        action_path = repo_root / ".github" / "actions" / "package" / "action.yml"
        return action_path.read_text(encoding="utf-8")

    def _build_action(self) -> str:
        repo_root = self._repo_root()
        action_path = repo_root / ".github" / "actions" / "build" / "action.yml"
        return action_path.read_text(encoding="utf-8")

    def _runner_action(self) -> str:
        repo_root = self._repo_root()
        action_path = repo_root / ".github" / "actions" / "runner" / "action.yml"
        return action_path.read_text(encoding="utf-8")

    def test_release_workflow_exposes_debian_distribution_and_urgency_env(self) -> None:
        workflow = self._workflow()
        self.assertIn("RELEASE_DEBIAN_DISTRIBUTION", workflow)
        self.assertIn("RELEASE_DEBIAN_URGENCY", workflow)

    def test_release_workflow_checkout_fetches_full_history_for_changelog_tags(self) -> None:
        workflow = self._workflow()
        self.assertIn("uses: actions/checkout@v4", workflow)
        self.assertIn("fetch-depth: 0", workflow)

    def test_release_workflow_is_parallel_ready_dag(self) -> None:
        workflow = self._workflow()
        self.assertIn("concurrency:", workflow)
        self.assertIn("cancel-in-progress: false", workflow)
        for job in (
            "validate-release-state:",
            "core-verify:",
            "release-tooling-verify:",
            "web-verify:",
            "docs-validate:",
            "release-artifacts:",
            "publish-debian:",
            "github-release:",
            "docs-publish:",
            "record-release-success:",
        ):
            self.assertIn(job, workflow)
        self.assertIn("needs:\n      - validate-release-state", workflow)
        self.assertIn("needs:\n      - validate-release-state\n      - release-artifacts", workflow)

    def test_release_success_is_recorded_only_after_publication_gates(self) -> None:
        workflow = self._workflow()
        record_job = workflow.split("record-release-success:", 1)[1]
        self.assertIn("needs:", record_job)
        self.assertIn("- publish-debian", record_job)
        self.assertIn("- github-release", record_job)
        self.assertIn("- docs-publish", record_job)
        self.assertIn("record-release-success", record_job)
        self.assertNotIn("always()", record_job)

    def test_release_state_installs_pmdocs_before_downstream_jobs(self) -> None:
        workflow = self._workflow()
        validate_job = workflow.split("validate-release-state:", 1)[1].split("core-verify:", 1)[0]
        install_step = validate_job.split("Install release-tooling dependencies", 1)[1].split("Validate versions", 1)[0]
        self.assertIn("apt-get update", install_step)
        self.assertIn("apt.valkyrianlabs.com", install_step)
        self.assertIn("apt-get install -y pmdocs", install_step)
        self.assertIn("pmdocs --version", install_step)

    def test_docs_publish_does_not_refresh_apt_metadata(self) -> None:
        workflow = self._workflow()
        docs_publish_job = workflow.split("docs-publish:", 1)[1].split("record-release-success:", 1)[0]
        self.assertIn("Verify pmdocs", docs_publish_job)
        self.assertIn("command -v pmdocs", docs_publish_job)
        self.assertIn("pmdocs --version", docs_publish_job)
        self.assertNotIn("apt-get update", docs_publish_job)
        self.assertNotIn("apt update", docs_publish_job)
        self.assertIn("pmdocs validate --source docs", docs_publish_job)
        self.assertIn("pmdocs push", docs_publish_job)

    def test_github_release_assets_are_prepared_via_deduped_manifest_step(self) -> None:
        workflow = self._workflow()
        self.assertIn("Prepare GitHub release asset list (deduped)", workflow)
        self.assertIn("id: gh_release_assets", workflow)
        self.assertIn("find release -type f | LC_ALL=C sort -u", workflow)

    def test_release_artifact_job_syncs_private_web_icons_before_packaging(self) -> None:
        workflow = self._workflow()
        artifact_job = workflow.split("release-artifacts:", 1)[1].split("publish-debian:", 1)[0]
        setup_index = artifact_job.index("uses: ./.github/actions/setup_web")
        icon_sync_index = artifact_job.index("uses: ./.github/actions/sync_web_icons")
        package_index = artifact_job.index("uses: ./.github/actions/package")
        self.assertLess(setup_index, icon_sync_index)
        self.assertLess(icon_sync_index, package_index)

    def test_cpp_build_actions_sync_private_web_icons_before_building(self) -> None:
        build_action = self._build_action()
        icon_sync_index = build_action.index("uses: ./.github/actions/sync_web_icons")
        meson_index = build_action.index("meson setup build")
        self.assertLess(icon_sync_index, meson_index)

        runner_action = self._runner_action()
        package_sync_index = runner_action.index("Sync private web icons for package")
        package_index = runner_action.index("uses: ./.github/actions/package")
        self.assertLess(package_sync_index, package_index)

    def test_github_release_action_uses_manifest_output_not_duplicate_globs(self) -> None:
        workflow = self._workflow()
        self.assertIn("files: ${{ steps.gh_release_assets.outputs.assets }}", workflow)
        self.assertIn("overwrite_files: true", workflow)
        self.assertIn("fail_on_unmatched_files: true", workflow)
        self.assertNotIn("release/**/**/*", workflow)
        self.assertNotIn("release/**/*", workflow)
        self.assertNotIn("release/*", workflow)

    def test_package_action_preflight_validates_debian_distribution_and_urgency_tokens(self) -> None:
        action = self._package_action()
        self.assertIn("RELEASE_DEBIAN_DISTRIBUTION", action)
        self.assertIn("RELEASE_DEBIAN_URGENCY", action)
        self.assertIn("debian_token_regex=", action)
        self.assertIn("RELEASE_DEBIAN_DISTRIBUTION must be a Debian token", action)
        self.assertIn("RELEASE_DEBIAN_URGENCY must be a Debian token", action)

    def test_package_action_clears_volatile_changelog_scratch_before_generation(self) -> None:
        action = self._package_action()
        self.assertIn("scratch_dir=\".changelog_scratch\"", action)
        self.assertIn("rm -rf \"$scratch_dir\" \"$artifact_dir\"", action)
        self.assertIn("clearing volatile changelog scratch", action)

    def test_package_action_writes_changelog_context_artifact(self) -> None:
        action = self._package_action()
        self.assertIn("--context-output", action)
        self.assertIn("changelog.context.json", action)
        self.assertIn("--semantic-payload-output", action)
        self.assertIn("changelog.semantic_payload.json", action)

    def test_package_action_resolves_and_passes_release_notes_base(self) -> None:
        action = self._package_action()
        self.assertIn("resolve-release-notes-base", action)
        self.assertIn("--release-notes-base", action)
        self.assertIn("--release-notes-base-resolution", action)
        self.assertIn("release_notes_base.resolution.json", action)

    def test_release_shell_blocks_do_not_use_python_heredocs(self) -> None:
        workflow = self._workflow()
        action = self._package_action()
        for content in (workflow, action):
            self.assertNotIn("<<'PY'", content)
            self.assertNotIn('<<"PY"', content)
            self.assertNotIn("python - <<", content)


if __name__ == "__main__":
    unittest.main()
