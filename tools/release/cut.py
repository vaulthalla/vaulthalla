"""Cut a release: bump, commit, annotated tag, and (with --push) atomically push commit + tag.

The tag push triggers `.github/workflows/release.yml`, the same workflow a release created in the
GitHub UI triggers. Every step is resumable: re-running after an interruption continues from the
state it finds (release commit present -> tag it; tag present locally -> push it) and refuses when
the tag already exists on the remote.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from email.utils import parsedate_to_datetime
from pathlib import Path
from typing import Callable, Sequence

from tools.release.version.models import Version
from tools.release.version.validate import get_release_state

RELEASE_MANAGED_FILES: tuple[str, ...] = ("VERSION", "meson.build", "web/package.json", "debian/changelog")
RELEASE_WORKFLOW_FILE = "release.yml"
RELEASE_SUBJECT_PATTERN = re.compile(r"^chore\(release\): v(?P<version>\d+\.\d+\.\d+)$")
BUMP_PARTS = ("major", "minor", "patch")

Runner = Callable[..., subprocess.CompletedProcess]
TestRunner = Callable[[Path], bool]


class ReleaseRefused(ValueError):
    """The repository is not in a state from which this release can be cut or resumed."""


@dataclass(frozen=True)
class CutReleaseOptions:
    target: str
    branch: str = "main"
    remote: str = "origin"
    push: bool = False
    skip_tests: bool = False
    fetch: bool = True


@dataclass(frozen=True)
class CutReleaseResult:
    version: str
    tag: str
    commit: str
    actions: tuple[str, ...]
    pushed: bool
    run_url: str | None = None


def release_commit_subject(version: str) -> str:
    return f"chore(release): v{version}"


def _default_runner(args: Sequence[str], *, cwd: Path, check: bool = True, timeout: float = 120.0,
                    **kwargs) -> subprocess.CompletedProcess:
    env = dict(os.environ)
    env.setdefault("GIT_TERMINAL_PROMPT", "0")
    completed = subprocess.run(
        list(args), cwd=cwd, text=True, capture_output=True, check=False, timeout=timeout, env=env, **kwargs
    )
    if check and completed.returncode != 0:
        detail = (completed.stderr or completed.stdout).strip()
        raise ReleaseRefused(f"`{' '.join(args)}` failed (exit {completed.returncode}): {detail}")
    return completed


def _default_test_runner(repo_root: Path) -> bool:
    from tools.release.suites import SUITES, render_outcomes, run_suites

    outcomes = run_suites(list(SUITES), repo_root=repo_root)
    print(render_outcomes(outcomes), end="")
    return all(outcome.ok for outcome in outcomes)


class _Git:
    def __init__(self, repo_root: Path, runner: Runner) -> None:
        self.root = repo_root
        self.run = runner

    def out(self, *args: str, check: bool = True, timeout: float = 120.0) -> str:
        return self.run(["git", *args], cwd=self.root, check=check, timeout=timeout).stdout.strip()

    def ok(self, *args: str) -> bool:
        return self.run(["git", *args], cwd=self.root, check=False).returncode == 0

    def rev(self, ref: str) -> str | None:
        completed = self.run(["git", "rev-parse", "-q", "--verify", f"{ref}^{{commit}}"], cwd=self.root, check=False)
        value = completed.stdout.strip()
        return value if completed.returncode == 0 and value else None


def resolve_target_version(current: Version, target: str) -> Version:
    if target in BUMP_PARTS:
        return {"major": current.bump_major, "minor": current.bump_minor, "patch": current.bump_patch}[target]()
    return Version.parse(target[1:] if target.startswith("v") else target)


def _read_version(repo_root: Path) -> Version:
    return Version.parse((repo_root / "VERSION").read_text(encoding="utf-8").strip())


def _debian_top_entry_date(repo_root: Path) -> datetime | None:
    path = repo_root / "debian" / "changelog"
    if not path.is_file():
        return None
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"^ -- .+?>\s{1,2}(\S.*)$", line)
        if match:
            try:
                return parsedate_to_datetime(match.group(1).strip())
            except (TypeError, ValueError):
                return None
    return None


def _check_release_state(repo_root: Path) -> None:
    state = get_release_state(repo_root)
    if state.has_structural_errors or state.has_drift:
        raise ReleaseRefused("`tools.release check` fails: release-managed files are missing or out of sync.")


def cut_release(
    repo_root: Path | str,
    options: CutReleaseOptions,
    *,
    runner: Runner = _default_runner,
    test_runner: TestRunner = _default_test_runner,
    gh_runner: Runner | None = None,
    sleep: Callable[[float], None] = time.sleep,
    log: Callable[[str], None] = print,
) -> CutReleaseResult:
    root = Path(repo_root).resolve()
    git = _Git(root, runner)
    actions: list[str] = []

    branch = git.out("rev-parse", "--abbrev-ref", "HEAD")
    if branch != options.branch:
        raise ReleaseRefused(f"On branch `{branch}`; releases are cut from `{options.branch}` (use --branch to override).")
    dirty = git.out("status", "--porcelain", "--untracked-files=no")
    if dirty:
        raise ReleaseRefused(f"Working tree has uncommitted changes to tracked files:\n{dirty}")

    if options.fetch:
        git.out("fetch", "--quiet", options.remote, options.branch, timeout=180)
    remote_ref = f"refs/remotes/{options.remote}/{options.branch}"
    remote_head = git.rev(remote_ref)
    if remote_head is None:
        raise ReleaseRefused(f"Remote branch {options.remote}/{options.branch} is unknown; fetch it first.")
    head = git.rev("HEAD")
    assert head is not None

    current = _read_version(root)
    head_subject = git.out("log", "-1", "--format=%s", "HEAD")
    head_release = RELEASE_SUBJECT_PATTERN.fullmatch(head_subject)
    head_unpushed = head != remote_head

    if options.target in BUMP_PARTS and head_release and head_unpushed:
        raise ReleaseRefused(
            f"HEAD is an unpushed release commit for v{head_release.group('version')}. "
            f"Resume it with `cut-release {head_release.group('version')}` instead of bumping again."
        )
    target = resolve_target_version(current, options.target)
    version = str(target)
    tag = f"v{version}"

    remote_tag = git.out("ls-remote", "--tags", options.remote, f"refs/tags/{tag}", timeout=60)
    if remote_tag:
        raise ReleaseRefused(
            f"{tag} already exists on {options.remote}; it has been released (or is releasing). "
            f"Check `python3 -m tools.release release-status {version}`."
        )

    local_tag_commit = git.rev(f"refs/tags/{tag}")
    parent = git.rev("HEAD~1")

    if local_tag_commit is not None:
        # Resume: tag exists locally only.
        if local_tag_commit != head:
            raise ReleaseRefused(f"Local tag {tag} points at {local_tag_commit[:12]}, not HEAD {head[:12]}.")
        if current != target:
            raise ReleaseRefused(f"Local tag {tag} exists but VERSION is {current}.")
        if head != remote_head and parent != remote_head:
            raise ReleaseRefused(
                f"Local tag {tag} is not exactly one commit ahead of {options.remote}/{options.branch}; refusing to push."
            )
        actions.append(f"resume: local tag {tag} found")
    elif head_release and head_release.group("version") == version and head_unpushed:
        # Resume: release commit exists, tag missing.
        if parent != remote_head:
            raise ReleaseRefused(
                f"The release commit for {tag} is not directly on top of {options.remote}/{options.branch}."
            )
        _check_release_state(root)
        git.out("tag", "-a", tag, "-m", f"Vaulthalla {tag}")
        actions.append(f"resume: tagged existing release commit as {tag}")
    else:
        # Fresh release.
        if head != remote_head:
            ahead = git.out("rev-list", "--count", f"{remote_ref}..HEAD")
            behind = git.out("rev-list", "--count", f"HEAD..{remote_ref}")
            raise ReleaseRefused(
                f"{options.branch} is not in sync with {options.remote}/{options.branch} "
                f"(ahead {ahead}, behind {behind}). Push or pull first."
            )
        if target <= current:
            raise ReleaseRefused(f"Target version {version} must be greater than the current VERSION {current}.")
        _check_release_state(root)
        actions.append("check: release-managed files in sync")
        if options.skip_tests:
            actions.append("tests: SKIPPED (--skip-tests)")
        else:
            log("[cut-release] running fast test suites (release tooling + lifecycle)")
            if not test_runner(root):
                raise ReleaseRefused("Fast test suites failed; not cutting a release.")
            actions.append("tests: release + lifecycle suites passed")

        previous_tag = f"v{current}"
        previous_at = git.out("log", "-1", "--format=%ct", f"{previous_tag}^{{commit}}", check=False)
        entry_at = _debian_top_entry_date(root)
        if previous_at.isdigit() and entry_at is not None:
            if entry_at < datetime.fromtimestamp(int(previous_at), tz=timezone.utc):
                log(
                    "[cut-release] WARNING: debian/changelog's top entry predates "
                    f"{previous_tag}. CI's manual changelog fallback will refuse it; the release needs the AI "
                    "path (OPENAI_API_KEY visible to release-artifacts) or a hand-written entry."
                )

        from tools.release.cli_tools.commands.version import cmd_set_version

        rc = cmd_set_version(
            argparse.Namespace(repo_root=str(root), version=version, debian_revision=1, dry_run=False)
        )
        if rc != 0:
            raise ReleaseRefused(f"set-version {version} failed (exit {rc}).")
        changed = sorted(line for line in git.out("diff", "--name-only", "HEAD").splitlines() if line)
        if changed != sorted(RELEASE_MANAGED_FILES):
            raise ReleaseRefused(
                "Version bump touched an unexpected file set "
                f"{changed}; expected exactly {sorted(RELEASE_MANAGED_FILES)}. Inspect and reset before retrying."
            )
        git.out("add", "--", *RELEASE_MANAGED_FILES)
        git.out(
            "commit",
            "-q",
            "-m",
            release_commit_subject(version),
            "-m",
            f"Bump version to {version} (VERSION, meson.build, web/package.json, debian/changelog).",
        )
        actions.append(f"commit: {release_commit_subject(version)}")
        git.out("tag", "-a", tag, "-m", f"Vaulthalla {tag}")
        actions.append(f"tag: annotated {tag}")

    commit = git.rev("HEAD") or ""
    push_command = f"git push --atomic {options.remote} HEAD:refs/heads/{options.branch} refs/tags/{tag}"
    if not options.push:
        actions.append(f"push: not requested; run `{push_command}` or re-run with --push")
        return CutReleaseResult(version=version, tag=tag, commit=commit, actions=tuple(actions), pushed=False)

    git.out("push", "--atomic", options.remote, f"HEAD:refs/heads/{options.branch}", f"refs/tags/{tag}", timeout=300)
    actions.append(f"push: atomic {options.branch} + {tag} -> {options.remote}")
    run_url = find_release_run_url(root, tag, runner=runner, gh_runner=gh_runner, sleep=sleep)
    return CutReleaseResult(
        version=version, tag=tag, commit=commit, actions=tuple(actions), pushed=True, run_url=run_url
    )


def github_slug_from_remote(url: str) -> str | None:
    match = re.search(r"github\.com[:/](?P<slug>[^/\s]+/[^/\s]+?)(?:\.git)?/?$", url.strip())
    return match.group("slug") if match else None


def _gh_available(gh_runner: Runner | None) -> bool:
    return gh_runner is not None or shutil.which("gh") is not None


def list_release_runs(repo_root: Path, tag: str, *, gh_runner: Runner | None = None) -> list[dict]:
    run = gh_runner or _default_runner
    completed = run(
        [
            "gh", "run", "list", "--workflow", RELEASE_WORKFLOW_FILE, "--branch", tag, "--limit", "5",
            "--json", "databaseId,url,status,conclusion,event,createdAt,headSha",
        ],
        cwd=repo_root,
        check=False,
        timeout=60,
    )
    if completed.returncode != 0:
        return []
    try:
        data = json.loads(completed.stdout or "[]")
    except json.JSONDecodeError:
        return []
    return data if isinstance(data, list) else []


def find_release_run_url(
    repo_root: Path,
    tag: str,
    *,
    runner: Runner = _default_runner,
    gh_runner: Runner | None = None,
    sleep: Callable[[float], None] = time.sleep,
    attempts: int = 6,
) -> str | None:
    if _gh_available(gh_runner):
        for attempt in range(attempts):
            runs = list_release_runs(repo_root, tag, gh_runner=gh_runner)
            if runs:
                return str(runs[0].get("url") or "") or None
            if attempt + 1 < attempts:
                sleep(5)
    remote_url = runner(["git", "remote", "get-url", "origin"], cwd=repo_root, check=False).stdout.strip()
    slug = github_slug_from_remote(remote_url)
    if slug is None:
        return None
    return f"https://github.com/{slug}/actions/workflows/{RELEASE_WORKFLOW_FILE}?query=branch%3A{tag}"


def release_status(
    repo_root: Path | str,
    version: str,
    *,
    watch: bool = False,
    gh_runner: Runner | None = None,
    log: Callable[[str], None] = print,
) -> int:
    root = Path(repo_root).resolve()
    tag = version if version.startswith("v") else f"v{version}"
    if not _gh_available(gh_runner):
        log("`gh` is not installed; open the Actions tab for the release workflow instead.")
        return 2
    runs = list_release_runs(root, tag, gh_runner=gh_runner)
    if not runs:
        log(f"No {RELEASE_WORKFLOW_FILE} run found for {tag} (is the tag pushed?).")
        return 1
    latest = runs[0]
    log(
        f"{tag}: run {latest.get('databaseId')} status={latest.get('status')} "
        f"conclusion={latest.get('conclusion') or '-'} event={latest.get('event')} {latest.get('url')}"
    )
    if not watch:
        if latest.get("status") != "completed":
            return 3
        return 0 if latest.get("conclusion") == "success" else 1
    run = gh_runner or (lambda args, **kw: subprocess.run(list(args), cwd=kw.get("cwd"), check=False))
    completed = run(
        ["gh", "run", "watch", str(latest.get("databaseId")), "--exit-status", "--interval", "30"],
        cwd=root,
        check=False,
        timeout=4 * 3600,
    )
    return int(completed.returncode)


def main_cut_release(args: argparse.Namespace) -> int:
    options = CutReleaseOptions(
        target=args.target,
        branch=args.branch,
        remote=args.remote,
        push=bool(args.push),
        skip_tests=bool(args.skip_tests),
        fetch=not bool(args.no_fetch),
    )
    try:
        result = cut_release(args.repo_root, options)
    except ReleaseRefused as exc:
        print(f"REFUSED: {exc}", file=sys.stderr)
        return 1
    print("Cut release")
    print("-----------")
    print(f"Version:  {result.version}")
    print(f"Tag:      {result.tag}")
    print(f"Commit:   {result.commit}")
    for action in result.actions:
        print(f"- {action}")
    if result.pushed:
        print(f"Release workflow: {result.run_url or '(open the Actions tab; run not visible yet)'}")
        print(f"Follow it with: python3 -m tools.release release-status {result.version} --watch")
    return 0


def main_release_status(args: argparse.Namespace) -> int:
    version = args.version or str(_read_version(Path(args.repo_root).resolve()))
    return release_status(args.repo_root, version, watch=bool(args.watch))
