from __future__ import annotations

from collections import defaultdict
from pathlib import Path

from tools.release.changelog.categorize import (
    CATEGORY_ORDER,
    categorize_path,
    detect_flags,
    detect_themes_for_paths,
    extract_subscopes,
)
from tools.release.changelog.git_collect import (
    get_commits_since_tag,
    get_head_sha,
    get_latest_tag,
    get_release_file_stats,
)
from tools.release.changelog.models import CategoryContext, CommitInfo, FileChange, ReleaseContext
from tools.release.changelog.scoring import score_file_change
from tools.release.changelog.snippets import extract_relevant_snippets


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

    snippet_map = extract_relevant_snippets(
        repo_root=repo_root,
        previous_tag=previous_tag,
        category_contexts=categories,
    )

    final_categories: dict[str, CategoryContext] = {}
    for name, context in categories.items():
        final_categories[name] = CategoryContext(
            name=context.name,
            commit_count=context.commit_count,
            insertions=context.insertions,
            deletions=context.deletions,
            commits=context.commits,
            files=context.files,
            snippets=snippet_map.get(name, []),
            detected_themes=context.detected_themes,
        )

    return ReleaseContext(
        version=version,
        previous_tag=previous_tag,
        head_sha=head_sha,
        commit_count=len(commits),
        categories=final_categories,
        cross_cutting_notes=[],
    )


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
        detected_themes = detect_themes_for_paths([file.path for file in file_changes])

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
