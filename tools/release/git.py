from __future__ import annotations

import subprocess
from collections import defaultdict
from pathlib import Path

from tools.release.categorize import (
    CATEGORY_ORDER,
    categorize_path,
    detect_flags,
    detect_themes_for_paths,
    extract_subscopes,
    normalize_path,
)
from tools.release.models import CategoryContext, CommitInfo, FileChange, ReleaseContext


FIELD_SEP = "\x1f"
RECORD_SEP = "\x1e"


def get_head_sha(repo_root: Path | str = ".") -> str:
    return _run_git(["rev-parse", "HEAD"], repo_root).strip()


def get_latest_tag(repo_root: Path | str = ".") -> str | None:
    try:
        result = _run_git(["describe", "--tags", "--abbrev=0"], repo_root).strip()
        return result or None
    except RuntimeError:
        return None


def build_release_context(
    version: str,
    repo_root: Path | str = ".",
    previous_tag: str | None = None,
) -> ReleaseContext:
    repo_root = Path(repo_root).resolve()
    previous_tag = previous_tag if previous_tag is not None else get_latest_tag(repo_root)
    head_sha = get_head_sha(repo_root)

    commits = get_commits_since_tag(repo_root, previous_tag)
    file_stats = get_release_file_stats(repo_root, previous_tag)
    file_commit_counts = get_file_commit_counts(commits)

    categories = build_category_contexts(
        commits=commits,
        file_stats=file_stats,
        file_commit_counts=file_commit_counts,
    )

    return ReleaseContext(
        version=version,
        previous_tag=previous_tag,
        head_sha=head_sha,
        commit_count=len(commits),
        categories=categories,
        cross_cutting_notes=[],
    )


def get_commits_since_tag(
    repo_root: Path | str = ".",
    previous_tag: str | None = None,
) -> list[CommitInfo]:
    repo_root = Path(repo_root).resolve()
    commit_range = _build_commit_range(previous_tag)

    pretty = f"%H{FIELD_SEP}%s{FIELD_SEP}%b{RECORD_SEP}"
    output = _run_git(["log", commit_range, f"--pretty=format:{pretty}"], repo_root)

    raw_records = [record.strip() for record in output.split(RECORD_SEP) if record.strip()]
    commits: list[CommitInfo] = []

    for record in raw_records:
        parts = record.split(FIELD_SEP)
        if len(parts) != 3:
            continue

        sha, subject, body = (part.strip() for part in parts)
        files, insertions, deletions = get_commit_file_summary(repo_root, sha)
        categories = sorted({categorize_path(path) for path in files})

        commits.append(
            CommitInfo(
                sha=sha,
                subject=subject,
                body=body,
                files=files,
                insertions=insertions,
                deletions=deletions,
                categories=categories,
            )
        )

    return commits


def get_commit_file_summary(
    repo_root: Path | str,
    sha: str,
) -> tuple[list[str], int, int]:
    sha = sha.strip()
    output = _run_git(["show", "--numstat", "--format=", sha], repo_root)

    files: list[str] = []
    insertions = 0
    deletions = 0

    for line in output.splitlines():
        parts = line.split("\t")
        if len(parts) != 3:
            continue

        added_raw, deleted_raw, path = parts
        normalized_path = normalize_path(path)

        files.append(normalized_path)

        if added_raw.isdigit():
            insertions += int(added_raw)

        if deleted_raw.isdigit():
            deletions += int(deleted_raw)

    return files, insertions, deletions


def get_release_file_stats(
    repo_root: Path | str = ".",
    previous_tag: str | None = None,
) -> dict[str, tuple[int, int]]:
    repo_root = Path(repo_root).resolve()
    commit_range = _build_commit_range(previous_tag)

    output = _run_git(["diff", "--numstat", commit_range], repo_root)
    stats: dict[str, tuple[int, int]] = {}

    for line in output.splitlines():
        parts = line.split("\t")
        if len(parts) != 3:
            continue

        added_raw, deleted_raw, path = parts
        normalized_path = normalize_path(path)

        added = int(added_raw) if added_raw.isdigit() else 0
        deleted = int(deleted_raw) if deleted_raw.isdigit() else 0

        stats[normalized_path] = (added, deleted)

    return stats


def get_file_commit_counts(commits: list[CommitInfo]) -> dict[str, int]:
    counts: dict[str, int] = defaultdict(int)

    for commit in commits:
        seen_in_commit = set(commit.files)
        for path in seen_in_commit:
            counts[path] += 1

    return dict(counts)


def build_category_contexts(
    *,
    commits: list[CommitInfo],
    file_stats: dict[str, tuple[int, int]],
    file_commit_counts: dict[str, int],
) -> dict[str, CategoryContext]:
    commit_map: dict[str, list[CommitInfo]] = defaultdict(list)
    path_map: dict[str, list[str]] = defaultdict(list)

    for commit in commits:
        for category in commit.categories:
            commit_map[category].append(commit)

        for path in commit.files:
            category = categorize_path(path)
            path_map[category].append(path)

    categories: dict[str, CategoryContext] = {}

    for category in CATEGORY_ORDER:
        category_commits = commit_map.get(category, [])
        category_paths = sorted(set(path_map.get(category, [])))

        if not category_commits and not category_paths:
            continue

        file_changes: list[FileChange] = []
        total_insertions = 0
        total_deletions = 0

        for path in category_paths:
            insertions, deletions = file_stats.get(path, (0, 0))
            total_insertions += insertions
            total_deletions += deletions

            flags = detect_flags(path)
            subscopes = extract_subscopes(path, category)
            score = score_file_change(
                path=path,
                category=category,
                insertions=insertions,
                deletions=deletions,
                commit_count=file_commit_counts.get(path, 0),
                flags=flags,
            )

            file_changes.append(
                FileChange(
                    path=path,
                    category=category,
                    subscopes=subscopes,
                    insertions=insertions,
                    deletions=deletions,
                    commit_count=file_commit_counts.get(path, 0),
                    score=score,
                    flags=flags,
                )
            )

        file_changes.sort(key=lambda item: item.score, reverse=True)

        all_paths_for_themes = [file.path for file in file_changes]
        detected_themes = detect_themes_for_paths(all_paths_for_themes)

        categories[category] = CategoryContext(
            name=category,
            commit_count=len(category_commits),
            insertions=total_insertions,
            deletions=total_deletions,
            commits=category_commits,
            files=file_changes,
            snippets=[],
            detected_themes=detected_themes,
        )

    return categories


def score_file_change(
    *,
    path: str,
    category: str,
    insertions: int,
    deletions: int,
    commit_count: int,
    flags: tuple[str, ...],
) -> float:
    score = 0.0
    change_volume = insertions + deletions

    score += min(change_volume, 400) / 20.0
    score += commit_count * 1.5

    lower = path.lower()

    if category == "core":
        if lower.startswith("core/include/"):
            score += 3.0
        if lower.startswith("core/src/"):
            score += 2.5
        if "fuse" in lower:
            score += 2.0

    elif category == "web":
        if lower.startswith("web/src/app/"):
            score += 3.0
        if lower.startswith("web/src/components/"):
            score += 2.0
        if "auth" in lower or "middleware" in lower:
            score += 2.0

    elif category == "tools":
        if lower.startswith("tools/release/"):
            score += 3.0
        if lower.endswith("cli.py") or lower.endswith("validate.py"):
            score += 2.0

    elif category == "debian":
        if lower in {"debian/control", "debian/rules", "debian/install"}:
            score += 3.0
        if lower.endswith(("postinst", "postrm", "preinst.ex", "prerm.ex")):
            score += 2.0

    elif category == "deploy":
        if "/psql/" in f"/{lower}":
            score += 3.5
        if "config" in lower:
            score += 2.5
        if "systemd" in lower:
            score += 2.5
        if lower.startswith("bin/"):
            score += 2.0

    if "database" in flags:
        score += 2.5
    if "config" in flags:
        score += 2.0
    if "systemd" in flags:
        score += 2.0
    if "packaging" in flags:
        score += 2.0
    if "release-tooling" in flags:
        score += 2.0
    if "api-surface" in flags:
        score += 1.5
    if "frontend-routing" in flags:
        score += 1.5

    if _is_noise_path(lower):
        score -= 10.0

    return round(score, 2)


def _is_noise_path(path: str) -> bool:
    if path.startswith("build/"):
        return True

    noisy_suffixes = (
        ".lock",
        ".min.js",
        ".map",
        ".png",
        ".jpg",
        ".jpeg",
        ".gif",
        ".svg",
        ".webp",
    )
    if path.endswith(noisy_suffixes):
        return True

    noisy_parts = (
        "/node_modules/",
        "/dist/",
        "/vendor/",
        "/coverage/",
        "/test-assets/",
    )
    return any(part in f"/{path}" for part in noisy_parts)


def _build_commit_range(previous_tag: str | None) -> str:
    return f"{previous_tag}..HEAD" if previous_tag else "HEAD"


def _run_git(args: list[str], repo_root: Path | str = ".") -> str:
    repo_root = Path(repo_root).resolve()

    result = subprocess.run(
        ["git", *args],
        cwd=repo_root,
        text=True,
        capture_output=True,
        check=False,
    )

    if result.returncode != 0:
        stderr = result.stderr.strip()
        raise RuntimeError(f"git {' '.join(args)} failed: {stderr}")

    return result.stdout


if __name__ == "__main__":
    import sys
    import json
    from dataclasses import asdict
    from pathlib import Path

    repo_root = Path(__file__).resolve().parents[2]

    from tools.release.files.version_file import read_version_file

    version = read_version_file(repo_root / "VERSION")

    try:
        context = build_release_context(version=str(version), repo_root=repo_root)
    except Exception as e:
        print(f"ERROR building release context: {e}")
        raise

    print("\n=== RELEASE CONTEXT DEBUG ===\n")
    print(f"Version:        {context.version}")
    print(f"Previous tag:   {context.previous_tag}")
    print(f"HEAD:           {context.head_sha}")
    print(f"Commit count:   {context.commit_count}")
    print()

    for name, cat in context.categories.items():
        print(f"--- CATEGORY: {name} ---")
        print(f"Commits:      {cat.commit_count}")
        print(f"Insertions:   {cat.insertions}")
        print(f"Deletions:    {cat.deletions}")
        print(f"Themes:       {', '.join(cat.detected_themes) or 'none'}")

        print("\nTop Files:")
        for f in cat.files[:5]:
            print(
                f"  - {f.path} "
                f"(score={f.score}, +{f.insertions}/-{f.deletions}, commits={f.commit_count}, flags={list(f.flags)})"
            )

        print("\nRecent Commits:")
        for c in cat.commits[:5]:
            print(f"  - {c.subject} ({c.sha[:7]})")

        print("\n")

    if "--json" in sys.argv:
        print("\n=== FULL JSON ===\n")
        print(json.dumps(asdict(context), indent=2))
