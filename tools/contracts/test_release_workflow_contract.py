"""Contract: the release workflow is a thin vl-release transaction with the safety properties releases rely on.

Release semantics live in `vlr` (release.toml); the workflow provides triggers, runners, credentials and ordering.
Parsed as text (no YAML dependency): jobs are the `  <name>:` blocks under `jobs:`.
"""

from __future__ import annotations

from pathlib import Path
import re
import tomllib
import unittest

REPO_ROOT = Path(__file__).resolve().parents[2]
DISPATCH_REF = "${{ github.event.inputs.ref || github.ref }}"


def _read(relative: str) -> str:
    return (REPO_ROOT / relative).read_text(encoding="utf-8")


def _jobs(workflow: str) -> dict[str, str]:
    body = workflow.split("\njobs:\n", 1)[1]
    parts = re.split(r"(?m)^  ([a-z0-9-]+):\n", body)
    return {parts[i]: parts[i + 1] for i in range(1, len(parts), 2)}


def _needs(job: str) -> set[str]:
    match = re.search(r"(?m)^    needs:(?: (\S+)\n|\n((?:      - \S+\n)+))", job)
    if not match:
        return set()
    if match.group(1):
        return {match.group(1)}
    return {line.strip()[2:] for line in match.group(2).splitlines()}


class ReleaseWorkflowContractTests(unittest.TestCase):
    workflow = _read(".github/workflows/release.yml")
    jobs = _jobs(workflow)

    def test_tag_pushes_and_dispatch_start_it(self) -> None:
        self.assertRegex(self.workflow, r"push:\n    tags:\n      - \"v\*\"")
        self.assertIn("workflow_dispatch:", self.workflow)

    def test_release_semantics_are_vl_release_commands(self) -> None:
        self.assertNotIn("tools.release", self.workflow)
        self.assertNotIn("OPENAI", self.workflow)
        artifacts = self.jobs["release-artifacts"]
        order = [artifacts.index(cmd) for cmd in ("vlr prepare --record", "vlr build-deb", "vlr checksums",
                                                  "vlr validate-artifacts")]
        self.assertEqual(order, sorted(order), "prepare, then build, then checksums, then validate")
        self.assertIn("vlr check --release --tag", self.jobs["release-check"])

    def test_artifacts_build_only_after_every_verification(self) -> None:
        self.assertEqual(
            _needs(self.jobs["release-artifacts"]),
            {"release-check", "contracts-verify", "web-verify", "docs-validate"},
        )

    def test_release_builds_once_at_o3_and_never_reruns_the_test_suite(self) -> None:
        # The required PR gate compiles and runs the C++ suite at -O0; a release builds the -O3 package once and
        # validates it. No core-verify job, no second meson build, no meson test.
        self.assertNotIn("core-verify", self.jobs)
        for name, job in self.jobs.items():
            self.assertNotRegex(job, r"uses: \./\.github/actions/(?:build|test)\s*\n", name)
            self.assertNotIn("meson test", job, name)
        artifacts = self.jobs["release-artifacts"]
        self.assertLess(artifacts.index("vlr build-deb"),
                        artifacts.index("python3 tools/dev/check_build_flags.py release/build-deb.log"))

    def test_pr_gate_builds_at_o0_with_werror_and_runs_the_suite(self) -> None:
        action = _read(".github/actions/build/action.yml")
        self.assertRegex(action, r"build_type:\n(?:.*\n)*?    default: debug\n")
        self.assertIn("--buildtype=${{ inputs.build_type }} -Dwerror=true -Dbuild_unit_tests=true", action)
        runner = _read(".github/actions/runner/action.yml")
        self.assertRegex(runner, r"uses: \./\.github/actions/build\n\s+with:\n\s+build_type: debug")
        self.assertIn("meson test -C build", _read(".github/actions/test/action.yml"))
        self.assertIn("test: 'true'", _read(".github/workflows/build_and_test.yml"))

    def test_publication_order_and_finalize_last(self) -> None:
        self.assertEqual(_needs(self.jobs["publish-debian"]), {"release-check", "release-artifacts"})
        self.assertIn("publish-debian", _needs(self.jobs["github-release"]))
        self.assertEqual(_needs(self.jobs["finalize"]), {"release-check", "publish-debian", "github-release"})
        finalize = self.jobs["finalize"]
        self.assertIn("needs.publish-debian.result == 'success'", finalize)
        self.assertIn("needs.github-release.result == 'success'", finalize)
        self.assertIn("vlr finalize --record release/meta/prepare.json", finalize)

    def test_github_release_job_installs_the_github_cli(self) -> None:
        # vlr github-release shells out to gh; the self-hosted runner has none (v1.8.0's first run failed there).
        self.assertIn("packages: vl-release gh", self.jobs["github-release"])

    def test_docs_failure_cannot_block_or_fail_the_release_record(self) -> None:
        self.assertNotIn("docs-publish", _needs(self.jobs["finalize"]))
        self.assertNotIn("finalize", _needs(self.jobs["docs-publish"]))

    def test_publish_is_idempotent_verified_and_serialized_across_tags(self) -> None:
        publish = self.jobs["publish-debian"]
        self.assertIn("vlr publish-deb --require-enabled", publish)
        self.assertIn("vlr checksums --verify", publish)
        self.assertRegex(publish, r"concurrency:\n      group: vaulthalla-apt-publish\n      cancel-in-progress: false")
        self.assertIn("environment: Production", publish)

    def test_every_job_has_a_timeout(self) -> None:
        for name, job in self.jobs.items():
            with self.subTest(job=name):
                self.assertRegex(job, r"(?m)^    timeout-minutes: \d+$")

    def test_secrets_are_step_scoped(self) -> None:
        workflow_env = self.workflow.split("\njobs:\n", 1)[0]
        self.assertNotIn("secrets.", workflow_env)
        for name, job in self.jobs.items():
            for match in re.finditer(r"secrets\.", job):
                line_start = job.rfind("\n", 0, match.start()) + 1
                indent = len(job[line_start:]) - len(job[line_start:].lstrip())
                with self.subTest(job=name):
                    self.assertGreaterEqual(indent, 10, "secrets belong in a step's env, not the job's")

    def test_least_privilege_writes(self) -> None:
        self.assertRegex(self.workflow, r"(?m)^permissions:\n  contents: read$")
        writers = {name for name, job in self.jobs.items() if "contents: write" in job}
        self.assertEqual(writers, {"github-release", "finalize"})

    def test_every_checkout_honors_the_dispatch_ref(self) -> None:
        checkouts = re.findall(r"uses: actions/checkout@v4\n        with:\n          ref: (.+)\n", self.workflow)
        self.assertEqual(len(checkouts), self.workflow.count("uses: actions/checkout@v4"))
        self.assertTrue(all(ref == DISPATCH_REF for ref in checkouts))

    def test_composite_build_action_does_not_recheckout(self) -> None:
        # A nested checkout without `ref` reset the workspace to github.sha, ignoring the dispatch ref.
        self.assertNotIn("actions/checkout", _read(".github/actions/build/action.yml"))

    def test_python_suites_run_with_count_floors_and_shellcheck_in_ci_and_release(self) -> None:
        for path in (".github/workflows/release.yml", ".github/workflows/build_and_test.yml"):
            text = _read(path)
            with self.subTest(path=path):
                self.assertIn("bash tools/dev/verify.sh release packaging lifecycle", text)
                self.assertIn("bash .github/scripts/shellcheck.sh", text)
                self.assertNotIn("unittest discover", text)
        self.assertIn("run_suite", _read("tools/dev/verify.sh"))

    def test_ci_apt_calls_wait_for_the_dpkg_lock(self) -> None:
        # Self-hosted runners run their own apt maintenance; apt's default 120s lock wait failed builds.
        for path in (".github/workflows/release.yml", ".github/workflows/build_and_test.yml",
                     ".github/actions/setup_valkyrianlabs_tools/action.yml", "bin/setup/install_deps.sh"):
            text = _read(path)
            for match in re.finditer(r"\bapt(?:-get)?\s+(?:-\S+\s+\S+\s+)*(update|install)\b[^\n]*", text):
                line = match.group(0)
                with self.subTest(path=path, line=line):
                    self.assertTrue("DPkg::Lock::Timeout" in line or "APT_LOCK_OPTS" in line)

    def test_cpp_and_web_builds_sync_private_icons_through_one_script(self) -> None:
        self.assertIn("web/bin/sync_private_icons.sh", _read(".github/actions/sync_web_icons/action.yml"))
        self.assertIn("sync_private_icons.sh", _read("web/bin/build_release_payload.sh"))
        for action in ("build", "build_web"):
            with self.subTest(action=action):
                self.assertIn("./.github/actions/sync_web_icons", _read(f".github/actions/{action}/action.yml"))


class ReleaseTomlContractTests(unittest.TestCase):
    config = tomllib.loads(_read("release.toml"))

    def test_every_version_file_is_managed(self) -> None:
        self.assertEqual(self.config["version"]["canonical"], "VERSION")
        targets = {(t["kind"], t["path"]) for t in self.config["version"]["targets"]}
        self.assertEqual(targets, {("meson", "meson.build"), ("package_json", "web/package.json")})

    def test_package_contract_keeps_the_product_invariants(self) -> None:
        (package,) = self.config["debian"]["packages"]
        self.assertEqual(package["name"], "vaulthalla")
        for path in ("usr/bin/vh", "usr/lib/vaulthalla/lifecycle", "usr/share/vaulthalla-web/server.js",
                     "usr/share/vaulthalla/psql/000_schema.sql", "lib/systemd/system/vaulthalla.service"):
            self.assertIn(path, package["required_paths"])
        for path in ("lib/systemd/system/vaulthalla-cli.socket", "lib/systemd/system/vaulthalla-cli.service"):
            self.assertIn(path, package["forbidden_paths"])  # #110
        self.assertIn(
            {"member": "usr/share/vaulthalla/config/config.yaml", "source": "deploy/config/config.yaml"},
            package["identical_files"],
        )

    def test_local_secrets_never_reach_the_build_tree(self) -> None:
        excluded = set(self.config["debian"]["build_excludes"])
        for path in (".bashrc", "cloudflare.ini", "config.yaml", "deploy/vaulthalla.env", "deploy/bashrc"):
            self.assertIn(path, excluded)

    def test_web_payload_is_built_before_packaging_and_shipped(self) -> None:
        self.assertEqual(self.config["debian"]["pre_build"],
                         [["bash", "web/bin/build_release_payload.sh", "build/web-payload"]])
        self.assertEqual(self.config["debian"]["extra_artifacts"],
                         ["build/web-payload/vaulthalla-web_*_next-standalone.tar.gz"])

    def test_apt_publication_target(self) -> None:
        self.assertEqual(self.config["publish"]["apt"]["repository_url"], "https://apt.vaulthalla.sh")

    def test_published_history_is_only_written_by_vl_release(self) -> None:
        changelog = _read("debian/changelog")
        top = changelog.splitlines()[0]
        self.assertRegex(top, r"^vaulthalla \(\d+\.\d+\.\d+-\d+\) ")
        version = re.search(r"\((\d+\.\d+\.\d+)-\d+\)", top).group(1)
        current = _read("VERSION").strip()
        # The top entry is a published release: older than VERSION while a release is staged, equal once recorded.
        self.assertLessEqual(tuple(map(int, version.split("."))), tuple(map(int, current.split("."))))


if __name__ == "__main__":
    unittest.main()
