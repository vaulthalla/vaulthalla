from __future__ import annotations

import re
from pathlib import Path

from tools.release.changelog.git_collect import get_file_patch
from tools.release.changelog.models import CategoryContext, DiffSnippet, FileChange
from tools.release.changelog.scoring import score_patch_hunk

HUNK_HEADER_RE = re.compile(r"^@@ .* @@.*$", re.MULTILINE)


def split_patch_into_hunks(patch: str) -> list[str]:
    if not patch.strip():
        return []

    matches = list(HUNK_HEADER_RE.finditer(patch))
    if not matches:
        return [patch.strip()]

    hunks: list[str] = []
    for index, match in enumerate(matches):
        start = match.start()
        end = matches[index + 1].start() if index + 1 < len(matches) else len(patch)
        hunks.append(patch[start:end].strip())

    return hunks


def build_snippet_reason(file_change: FileChange, hunk: str, category: str) -> str:
    del hunk  # Reason currently depends on file-level signals.

    reasons = [f"Selected from high-scoring {category} file"]
    if file_change.commit_count > 1:
        reasons.append(f"touched in {file_change.commit_count} commits")
    if file_change.flags:
        reasons.append(f"flags: {', '.join(file_change.flags)}")
    return "; ".join(reasons)


def extract_relevant_snippets(
    repo_root: Path | str,
    previous_tag: str | None,
    category_contexts: dict[str, CategoryContext],
    max_files_per_category: int = 5,
    max_hunks_per_file: int = 2,
) -> dict[str, list[DiffSnippet]]:
    repo_root = Path(repo_root).resolve()
    results: dict[str, list[DiffSnippet]] = {}

    for category_name, context in category_contexts.items():
        snippets: list[DiffSnippet] = []
        candidate_files = context.files[:max_files_per_category]

        for file_change in candidate_files:
            patch = get_file_patch(repo_root, file_change.path, previous_tag)
            if not patch.strip():
                continue

            hunks = split_patch_into_hunks(patch)
            if not hunks:
                continue

            ranked_hunks = sorted(
                hunks,
                key=lambda hunk: score_patch_hunk(hunk, category_name, file_change.path),
                reverse=True,
            )

            for hunk in ranked_hunks[:max_hunks_per_file]:
                snippet_score = score_patch_hunk(hunk, category_name, file_change.path)
                reason = build_snippet_reason(file_change, hunk, category_name)
                snippets.append(
                    DiffSnippet(
                        path=file_change.path,
                        category=category_name,
                        subscopes=file_change.subscopes,
                        score=snippet_score,
                        reason=reason,
                        patch=hunk,
                        flags=file_change.flags,
                    )
                )

        snippets.sort(key=lambda item: item.score, reverse=True)
        results[category_name] = snippets

    return results
