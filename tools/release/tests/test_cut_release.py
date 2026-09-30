from __future__ import annotations

import json
import os
import subprocess
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from tools.release.cut import (
    CutReleaseOptions,
    ReleaseRefused,
    cut_release,
    github_slug_from_remote,
    release_status,
)

_GIT_ENV = {
    "GIT_AUTHOR_NAME": "Release Test",
    "GIT_AUTHOR_EMAIL": "release@example.com",
    "GIT_COMMITTER_NAME": "Release Test",
    "GIT_COMMITTER_EMAIL": "release@example.com",
    "GIT_CONFIG_GLOBAL": "/dev/null",
    "GIT_CONFIG_SYSTEM": "/dev/null",
}


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def _git(cwd: Path, *args: str) -> str:
    return subprocess.run(
        ["git", *args], cwd=cwd, check=True, capture_output=True, text=True, env={**os.environ, **_GIT_ENV}
    ).stdout.strip()


def _passing_tests(_root: Path) -> bool:
    return True


def _failing_tests(_root: Path) -> bool:
    return False


class FakeGh:
    def __init__(self, runs: list[dict] | None = None) -> None:
        self.runs = runs or []
        self.calls: list[list[str]] = []

    def __call__(self, args, **_kwargs):
        self.calls.append(list(args))
        if args[:3] == ["gh", "run", "list"]:
            return subprocess.CompletedProcess(args, 0, json.dumps(self.runs), "")
        if args[:3] == ["gh", "run", "watch"]:
            return subprocess.CompletedProcess(args, 0, "", "")
        return subprocess.CompletedProcess(args, 1, "", "unexpected")


class CutReleaseTests(unittest.TestCase):
    def setUp(self) -> None:
        self._env = patch.dict(os.environ, _GIT_ENV)
        self._env.start()
        self._tmp = TemporaryDirectory()
        base = Path(self._tmp.name)
        self.remote = base / "remote.git"
        self.repo = base / "work"
        _git(base, "init", "-q", "--bare", "-b", "main", str(self.remote))
        _git(base, "init", "-q", "-b", "main", str(self.repo))
        _write(self.repo / "VERSION", "1.2.3\n")
        _write(self.repo / "meson.build", "project('vaulthalla', 'cpp',\n  version: '1.2.3'\n)\n")
        _write(self.repo / "web" / "package.json", '{\n  "name": "vaulthalla-web",\n  "version": "1.2.3"\n}\n')
        _write(
            self.repo / "debian" / "changelog",
            "vaulthalla (1.2.3-1) unstable; urgency=medium\n\n  * entry\n\n"
            " -- Test User <test@example.com>  Sun, 19 Apr 2026 00:00:00 +0000\n",
        )
        _write(self.repo / "README.md", "readme\n")
        _git(self.repo, "add", "-A")
        _git(self.repo, "commit", "-q", "-m", "initial")
        _git(self.repo, "tag", "-a", "v1.2.3", "-m", "v1.2.3")
        _git(self.repo, "remote", "add", "origin", str(self.remote))
        _git(self.repo, "push", "-q", "origin", "main", "refs/tags/v1.2.3")

    def tearDown(self) -> None:
        self._tmp.cleanup()
        self._env.stop()

    def _cut(self, target: str, **kwargs):
        test_runner = kwargs.pop("test_runner", _passing_tests)
        gh = kwargs.pop("gh_runner", FakeGh())
        with redirect_stdout(StringIO()):
            return cut_release(
                self.repo,
                CutReleaseOptions(target=target, **kwargs),
                test_runner=test_runner,
                gh_runner=gh,
                sleep=lambda _s: None,
                log=lambda _line: None,
            )

    def _remote_tags(self) -> str:
        return _git(self.repo, "ls-remote", "--tags", "origin")

    def test_patch_release_commits_exactly_the_managed_files_and_tags_locally(self) -> None:
        result = self._cut("patch")

        self.assertEqual(result.version, "1.2.4")
        self.assertFalse(result.pushed)
        self.assertEqual((self.repo / "VERSION").read_text().strip(), "1.2.4")
        self.assertEqual(_git(self.repo, "log", "-1", "--format=%s"), "chore(release): v1.2.4")
        changed = sorted(_git(self.repo, "show", "--name-only", "--format=", "HEAD").splitlines())
        self.assertEqual(changed, ["VERSION", "debian/changelog", "meson.build", "web/package.json"])
        self.assertEqual(_git(self.repo, "cat-file", "-t", "v1.2.4"), "tag")  # annotated
        self.assertEqual(_git(self.repo, "rev-parse", "v1.2.4^{commit}"), _git(self.repo, "rev-parse", "HEAD"))
        self.assertNotIn("v1.2.4", self._remote_tags())
        self.assertTrue(any("git push --atomic origin" in action for action in result.actions))

    def test_push_is_atomic_and_reports_run_url(self) -> None:
        gh = FakeGh([{"databaseId": 7, "url": "https://github.com/o/r/actions/runs/7", "status": "queued"}])
        result = self._cut("minor", push=True, gh_runner=gh)

        self.assertEqual(result.version, "1.3.0")
        self.assertTrue(result.pushed)
        self.assertIn("refs/tags/v1.3.0", self._remote_tags())
        self.assertEqual(_git(self.remote, "rev-parse", "main"), _git(self.repo, "rev-parse", "HEAD"))
        self.assertEqual(result.run_url, "https://github.com/o/r/actions/runs/7")
        self.assertIn(["--branch", "v1.3.0"], [c[5:7] for c in gh.calls])

    def test_resume_pushes_a_local_only_tag(self) -> None:
        self._cut("patch")
        result = self._cut("1.2.4", push=True)
        self.assertTrue(result.pushed)
        self.assertTrue(any("resume: local tag v1.2.4" in action for action in result.actions))
        self.assertIn("refs/tags/v1.2.4", self._remote_tags())

    def test_resume_tags_an_untagged_release_commit(self) -> None:
        self._cut("patch")
        _git(self.repo, "tag", "-d", "v1.2.4")
        result = self._cut("1.2.4")
        self.assertTrue(any("resume: tagged existing release commit" in action for action in result.actions))
        self.assertEqual(_git(self.repo, "cat-file", "-t", "v1.2.4"), "tag")

    def test_bump_part_on_unpushed_release_commit_is_refused(self) -> None:
        self._cut("patch")
        with self.assertRaisesRegex(ReleaseRefused, "Resume it with `cut-release 1.2.4`"):
            self._cut("patch")

    def test_tag_already_on_remote_is_refused(self) -> None:
        self._cut("patch", push=True)
        with self.assertRaisesRegex(ReleaseRefused, "already exists on origin"):
            self._cut("1.2.4", push=True)

    def test_dirty_tree_is_refused(self) -> None:
        _write(self.repo / "README.md", "changed\n")
        with self.assertRaisesRegex(ReleaseRefused, "uncommitted changes"):
            self._cut("patch")

    def test_untracked_files_do_not_block(self) -> None:
        _write(self.repo / "scratch.txt", "local notes\n")
        self.assertEqual(self._cut("patch").version, "1.2.4")

    def test_wrong_branch_is_refused(self) -> None:
        _git(self.repo, "checkout", "-q", "-b", "feature")
        with self.assertRaisesRegex(ReleaseRefused, "releases are cut from `main`"):
            self._cut("patch")
        with self.assertRaisesRegex(ReleaseRefused, "remote ref feature"):
            self._cut("patch", branch="feature")

    def test_out_of_sync_with_remote_is_refused(self) -> None:
        _write(self.repo / "README.md", "ahead\n")
        _git(self.repo, "commit", "-q", "-am", "local only")
        with self.assertRaisesRegex(ReleaseRefused, r"not in sync with origin/main \(ahead 1, behind 0\)"):
            self._cut("patch")

    def test_failing_tests_abort_before_any_change(self) -> None:
        head = _git(self.repo, "rev-parse", "HEAD")
        with self.assertRaisesRegex(ReleaseRefused, "Fast test suites failed"):
            self._cut("patch", test_runner=_failing_tests)
        self.assertEqual(_git(self.repo, "rev-parse", "HEAD"), head)
        self.assertEqual((self.repo / "VERSION").read_text().strip(), "1.2.3")

    def test_explicit_version_must_be_greater(self) -> None:
        with self.assertRaisesRegex(ReleaseRefused, "must be greater"):
            self._cut("1.2.2")


class ReleaseHelpersTests(unittest.TestCase):
    def test_github_slug_from_remote(self) -> None:
        self.assertEqual(github_slug_from_remote("git@github.com:vaulthalla/vaulthalla.git"), "vaulthalla/vaulthalla")
        self.assertEqual(github_slug_from_remote("https://github.com/vaulthalla/vaulthalla"), "vaulthalla/vaulthalla")
        self.assertIsNone(github_slug_from_remote("/tmp/remote.git"))

    def test_release_status_reports_and_watches(self) -> None:
        with TemporaryDirectory() as temp_dir:
            done = FakeGh([{"databaseId": 9, "status": "completed", "conclusion": "success", "url": "u"}])
            self.assertEqual(release_status(temp_dir, "1.2.4", gh_runner=done, log=lambda _l: None), 0)
            running = FakeGh([{"databaseId": 9, "status": "in_progress", "conclusion": None, "url": "u"}])
            self.assertEqual(release_status(temp_dir, "1.2.4", gh_runner=running, log=lambda _l: None), 3)
            self.assertEqual(release_status(temp_dir, "v1.2.4", watch=True, gh_runner=running, log=lambda _l: None), 0)
            self.assertEqual(running.calls[-1][:4], ["gh", "run", "watch", "9"])
            self.assertEqual(release_status(temp_dir, "1.2.4", gh_runner=FakeGh([]), log=lambda _l: None), 1)


if __name__ == "__main__":
    unittest.main()
