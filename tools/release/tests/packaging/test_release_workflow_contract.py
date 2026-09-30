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

    def _job(self, name: str) -> dict:
        import yaml

        data = yaml.safe_load(self._workflow())
        return data["jobs"][name]

    def _jobs(self) -> dict:
        import yaml

        return yaml.safe_load(self._workflow())["jobs"]

    def test_release_success_is_recorded_only_after_publication_gates(self) -> None:
        workflow = self._workflow()
        record_job = workflow.split("record-release-success:", 1)[1]
        self.assertIn("needs:", record_job)
        self.assertIn("- publish-debian", record_job)
        self.assertIn("- github-release", record_job)
        self.assertIn("record-release-success", record_job)
        self.assertNotIn("always()", record_job)

    def test_docs_failure_cannot_mark_a_live_apt_release_failed(self) -> None:
        record = self._job("record-release-success")
        self.assertNotIn("docs-publish", record["needs"])
        self.assertNotIn("docs-publish", record["if"])
        docs = self._job("docs-publish")
        self.assertNotIn("github-release", docs["needs"])
        self.assertIn("publish-debian", docs["needs"])

    def test_every_job_has_a_timeout(self) -> None:
        for name, job in self._jobs().items():
            with self.subTest(job=name):
                self.assertIsInstance(job.get("timeout-minutes"), int)

    def test_secrets_are_step_scoped_not_workflow_env(self) -> None:
        import yaml

        data = yaml.safe_load(self._workflow())
        for key, value in data["env"].items():
            with self.subTest(env=key):
                self.assertNotIn("secrets.", str(value))
        self.assertNotIn("NEXUS_PASS", str(data["env"]))
        self.assertNotIn("OPENAI_API_KEY", str(data["env"]))
        publish_steps = self._job("publish-debian")["steps"]
        publish_step = next(step for step in publish_steps if "publish-deb" in str(step.get("run", "")))
        self.assertIn("secrets.NEXUS_PASS", str(publish_step["env"]))
        artifact_steps = self._job("release-artifacts")["steps"]
        package_step = next(step for step in artifact_steps if step.get("id") == "package")
        self.assertIn("secrets.OPENAI_API_KEY", str(package_step["env"]))
        self.assertEqual(data["permissions"].get("contents"), "read")

    def test_publication_is_serialized_across_tags(self) -> None:
        publish = self._job("publish-debian")
        self.assertEqual(publish["concurrency"]["group"], "vaulthalla-apt-publish")
        self.assertFalse(publish["concurrency"]["cancel-in-progress"])

    def test_publish_uses_idempotent_sha_verified_publisher(self) -> None:
        publish = self._job("publish-debian")
        rendered = str(publish["steps"])
        self.assertIn("--require-enabled", rendered)
        self.assertIn("--lab-evidence", rendered)

    def test_optional_lab_smoke_gate_is_default_off_and_cannot_skip_publication(self) -> None:
        lab = self._job("lab-smoke")
        self.assertEqual(lab["if"], "vars.VH_LAB_SMOKE_ENABLED == 'true'")
        self.assertIn("vh-lab", lab["runs-on"])
        self.assertEqual(lab["environment"], "Lab")
        self.assertIn("release-artifacts", lab["needs"])
        publish = self._job("publish-debian")
        self.assertIn("lab-smoke", publish["needs"])
        condition = publish["if"]
        self.assertIn("!cancelled()", condition)
        self.assertIn("needs.lab-smoke.result == 'skipped'", condition)
        self.assertIn("needs.lab-smoke.result == 'success'", condition)
        # Downstream jobs use explicit result checks so a skipped optional gate never skips them.
        for name in ("github-release", "docs-publish", "record-release-success"):
            with self.subTest(job=name):
                self.assertIn("!cancelled()", self._job(name)["if"])

    def test_composite_build_action_does_not_recheckout(self) -> None:
        # A nested checkout without `ref` reset the workspace to github.sha, ignoring dispatch `ref`.
        self.assertNotIn("actions/checkout", self._build_action())

    def test_every_checkout_in_release_workflow_honors_dispatch_ref(self) -> None:
        for name, job in self._jobs().items():
            for step in job.get("steps", []):
                if str(step.get("uses", "")).startswith("actions/checkout"):
                    with self.subTest(job=name):
                        self.assertEqual(step["with"]["ref"], "${{ github.event.inputs.ref || github.ref }}")

    def test_changelog_stage_is_time_bounded(self) -> None:
        action = self._package_action()
        self.assertIn("run_bounded()", action)
        self.assertEqual(action.count('if ! run_bounded "$PYTHON_BIN" -m tools.release changelog'), 3)

    def test_tooling_test_jobs_fetch_full_history_and_tags(self) -> None:
        import yaml

        checkout = self._job("release-tooling-verify")["steps"][0]
        self.assertEqual(checkout["with"]["fetch-depth"], 0)
        ci = yaml.safe_load(self._ci_workflow())
        self.assertEqual(ci["jobs"]["tooling"]["steps"][0]["with"]["fetch-depth"], 0)

    def test_legacy_local_publication_path_is_retired(self) -> None:
        # Exactly one publication path (CI): the legacy script must not upload, and `make release`
        # must not push. publish-deb is the only uploader and it refuses to overwrite published versions.
        script = (self._repo_root() / "bin" / "install_deb.sh").read_text(encoding="utf-8")
        self.assertNotIn("curl", script)
        self.assertNotIn("UPLOAD_PASS", script)
        self.assertIn("--push is retired", script)
        makefile = (self._repo_root() / "Makefile").read_text(encoding="utf-8")
        release_target = makefile.split("\nrelease:", 1)[1].split("\n\n", 1)[0]
        self.assertNotIn("--push", release_target)
        self.assertIn("cut-release", release_target)

    def test_github_release_requires_sha256sums_asset(self) -> None:
        self.assertIn("release/SHA256SUMS", str(self._job("github-release")["steps"]))

    def test_release_state_installs_pmdocs_before_downstream_jobs(self) -> None:
        workflow = self._workflow()
        validate_job = workflow.split("validate-release-state:", 1)[1].split("core-verify:", 1)[0]
        install_step = validate_job.split("Install release-tooling dependencies", 1)[1].split("Validate versions", 1)[0]
        self.assertRegex(install_step, r"apt-get (-o \S+ )*update")
        self.assertIn("apt.valkyrianlabs.com", install_step)
        self.assertRegex(install_step, r"apt-get (-o \S+ )*install -y pmdocs")
        self.assertIn("pmdocs --version", install_step)

    def test_ci_apt_calls_wait_for_the_dpkg_lock(self) -> None:
        # Self-hosted runners run their own apt maintenance; apt's default 120s lock wait failed PR builds.
        import re
        for path in (".github/workflows/release.yml", ".github/workflows/build_and_test.yml", "bin/setup/install_deps.sh"):
            text = (self._repo_root() / path).read_text(encoding="utf-8")
            for match in re.finditer(r"\bapt(?:-get)?\s+(?:-\S+\s+\S+\s+)*(update|install)\b[^\n]*", text):
                line = match.group(0)
                self.assertTrue("DPkg::Lock::Timeout" in line or "APT_LOCK_OPTS" in line, f"{path}: {line}")

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

    def _ci_workflow(self) -> str:
        return (self._repo_root() / ".github" / "workflows" / "build_and_test.yml").read_text(encoding="utf-8")

    def test_python_suites_run_through_count_guard_in_ci_and_release(self) -> None:
        for content in (self._workflow(), self._ci_workflow()):
            self.assertIn("python -m tools.release run-tests", content)
            # Raw discover silently skips non-package directories (P0-4); only the guarded runner is allowed.
            self.assertNotIn("unittest discover", content)

    def test_shellcheck_runs_in_ci_and_release(self) -> None:
        for content in (self._workflow(), self._ci_workflow()):
            self.assertIn("bash .github/scripts/shellcheck.sh", content)
        script = (self._repo_root() / ".github" / "scripts" / "shellcheck.sh").read_text(encoding="utf-8")
        for maintainer_script in ("debian/postinst", "debian/prerm", "debian/postrm"):
            self.assertIn(maintainer_script, script)

    def test_release_shell_blocks_do_not_use_python_heredocs(self) -> None:
        workflow = self._workflow()
        action = self._package_action()
        for content in (workflow, action):
            self.assertNotIn("<<'PY'", content)
            self.assertNotIn('<<"PY"', content)
            self.assertNotIn("python - <<", content)


if __name__ == "__main__":
    unittest.main()
