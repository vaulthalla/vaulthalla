from __future__ import annotations

from dataclasses import asdict, dataclass, field
import json
from typing import Any

from tools.release.changelog.categorize import CATEGORY_ORDER
from tools.release.changelog.models import CategoryContext, CommitInfo, DiffSnippet, FileChange, ReleaseContext

AI_PAYLOAD_SCHEMA_VERSION = "vaulthalla.release.ai_payload.v1"


@dataclass(frozen=True)
class PayloadLimits:
    max_categories: int | None = None
    max_commits_per_category: int = 3
    max_files_per_category: int = 3
    max_snippets_per_category: int = 2
    max_snippet_lines: int = 20
    max_snippet_chars: int = 800
    max_commit_subject_chars: int = 160
    max_snippet_reason_chars: int = 220


@dataclass(frozen=True)
class PayloadBuildConfig:
    limits: PayloadLimits = field(default_factory=PayloadLimits)
    include_weak_categories: bool = True


def build_ai_payload(
    context: ReleaseContext,
    config: PayloadBuildConfig | None = None,
) -> dict[str, Any]:
    """Build a deterministic, bounded payload projection from ReleaseContext.

    Contract:
    - Schema version is explicit (`AI_PAYLOAD_SCHEMA_VERSION`).
    - Category ordering is stable (`CATEGORY_ORDER` then alphabetical fallback).
    - Commit/file/snippet evidence uses ReleaseContext list order and fixed caps.
    - Truncation and category omission reasons are explicit in `generation.truncation`.
    - Output is model-ready and intentionally separate from debug/raw render formats.
    """
    cfg = config if config is not None else PayloadBuildConfig()
    ordered_categories = _ordered_categories(context)
    category_order_used = [name for name, _ in ordered_categories]

    omitted_for_weak_signal: list[str] = []
    candidate_categories: list[tuple[str, CategoryContext, str]] = []

    for name, category in ordered_categories:
        signal = classify_signal_strength(category)
        if signal == "weak" and not cfg.include_weak_categories:
            omitted_for_weak_signal.append(name)
            continue
        candidate_categories.append((name, category, signal))

    categories_truncated_by_limit: list[str] = []
    max_categories = cfg.limits.max_categories
    if max_categories is not None and len(candidate_categories) > max_categories:
        categories_truncated_by_limit = [name for name, _, _ in candidate_categories[max_categories:]]
        candidate_categories = candidate_categories[:max_categories]

    categories_payload: list[dict[str, Any]] = []
    categories_with_evidence_truncated: list[str] = []

    for name, category, signal in candidate_categories:
        payload = _build_category_payload(name, category, signal, cfg.limits)
        if payload["truncation"]["any_truncation"]:
            categories_with_evidence_truncated.append(name)
        categories_payload.append(payload)

    any_truncation = bool(
        categories_with_evidence_truncated or omitted_for_weak_signal or categories_truncated_by_limit
    )

    notes: list[str] = []
    if omitted_for_weak_signal:
        notes.append(
            "Weak-signal categories omitted by configuration: "
            + ", ".join(f"`{name}`" for name in omitted_for_weak_signal)
        )
    if categories_truncated_by_limit:
        notes.append(
            "Category cap applied; dropped categories: "
            + ", ".join(f"`{name}`" for name in categories_truncated_by_limit)
        )
    if not categories_payload:
        notes.append("No categories selected for payload after filters/limits.")

    return {
        "schema_version": AI_PAYLOAD_SCHEMA_VERSION,
        "metadata": {
            "version": context.version,
            "previous_tag": context.previous_tag,
            "head_sha": context.head_sha,
            "commit_count": context.commit_count,
        },
        "generation": {
            "category_order_used": category_order_used,
            "selected_category_order": [item["name"] for item in categories_payload],
            "limits_applied": asdict(cfg.limits),
            "include_weak_categories": cfg.include_weak_categories,
            "truncation": {
                "any_truncation": any_truncation,
                "categories_omitted_for_weak_signal": omitted_for_weak_signal,
                "categories_truncated_by_limit": categories_truncated_by_limit,
                "categories_with_evidence_truncated": categories_with_evidence_truncated,
            },
        },
        "categories": categories_payload,
        "notes": notes,
    }


def render_ai_payload_json(payload: dict[str, Any]) -> str:
    return json.dumps(payload, indent=2, sort_keys=False) + "\n"


def classify_signal_strength(category: CategoryContext) -> str:
    files_count = len(category.files)
    snippets_count = len(category.snippets)
    change_total = category.insertions + category.deletions

    if snippets_count >= 2 or change_total >= 80 or category.commit_count >= 4:
        return "strong"
    if snippets_count >= 1 or change_total >= 20 or category.commit_count >= 2 or files_count >= 3:
        return "moderate"
    return "weak"


def _build_category_payload(
    name: str,
    category: CategoryContext,
    signal_strength: str,
    limits: PayloadLimits,
) -> dict[str, Any]:
    commits_payload, commits_meta = _build_commits(category.commits, limits)
    files_payload, files_meta = _build_files(category.files, limits)
    snippets_payload, snippets_meta = _build_snippets(category.snippets, limits)

    any_truncation = (
        commits_meta["truncated"]
        or files_meta["truncated"]
        or snippets_meta["truncated"]
        or snippets_meta["excerpt_truncated_count"] > 0
    )

    return {
        "name": name,
        "summary": {
            "signal_strength": signal_strength,
            "commit_count": category.commit_count,
            "file_count": len(category.files),
            "snippet_count": len(category.snippets),
            "insertions": category.insertions,
            "deletions": category.deletions,
            "themes": sorted(category.detected_themes),
        },
        "evidence": {
            "top_commits": commits_payload,
            "top_files": files_payload,
            "top_snippets": snippets_payload,
        },
        "truncation": {
            "any_truncation": any_truncation,
            "commits": commits_meta,
            "files": files_meta,
            "snippets": snippets_meta,
        },
    }


def _build_commits(commits: list[CommitInfo], limits: PayloadLimits):
    selected = commits[: limits.max_commits_per_category]
    payload = []

    for commit in selected:
        subject, subject_truncated = _truncate_text(commit.subject.strip(), limits.max_commit_subject_chars)
        payload.append(
            {
                "sha": commit.sha[:7],
                "subject": subject,
                "subject_truncated": subject_truncated,
            }
        )

    return payload, _section_meta(
        input_count=len(commits),
        output_count=len(selected),
        cap=limits.max_commits_per_category,
    )


def _build_files(files: list[FileChange], limits: PayloadLimits):
    selected = files[: limits.max_files_per_category]
    payload = []

    for file_change in selected:
        payload.append(
            {
                "path": file_change.path,
                "insertions": file_change.insertions,
                "deletions": file_change.deletions,
                "score": file_change.score,
                "commit_count": file_change.commit_count,
                "flags": list(file_change.flags),
            }
        )

    return payload, _section_meta(
        input_count=len(files),
        output_count=len(selected),
        cap=limits.max_files_per_category,
    )


def _build_snippets(snippets: list[DiffSnippet], limits: PayloadLimits):
    selected = snippets[: limits.max_snippets_per_category]
    payload = []
    excerpt_truncated_count = 0

    for snippet in selected:
        reason, reason_truncated = _truncate_text(snippet.reason.strip(), limits.max_snippet_reason_chars)
        excerpt, excerpt_line_count, excerpt_char_count, excerpt_truncated = _build_snippet_excerpt(
            snippet.patch,
            limits,
        )
        if excerpt_truncated:
            excerpt_truncated_count += 1

        payload.append(
            {
                "path": snippet.path,
                "score": snippet.score,
                "reason": reason,
                "reason_truncated": reason_truncated,
                "flags": list(snippet.flags),
                "excerpt": excerpt,
                "excerpt_line_count": excerpt_line_count,
                "excerpt_char_count": excerpt_char_count,
                "excerpt_truncated": excerpt_truncated,
            }
        )

    meta = _section_meta(
        input_count=len(snippets),
        output_count=len(selected),
        cap=limits.max_snippets_per_category,
    )
    meta["excerpt_truncated_count"] = excerpt_truncated_count
    return payload, meta


def _build_snippet_excerpt(
    patch: str,
    limits: PayloadLimits,
) -> tuple[str, int, int, bool]:
    lines = patch.strip().splitlines() if patch.strip() else []
    truncated = False

    if len(lines) > limits.max_snippet_lines:
        lines = lines[: limits.max_snippet_lines]
        truncated = True

    excerpt = "\n".join(lines)
    if len(excerpt) > limits.max_snippet_chars:
        excerpt = excerpt[: limits.max_snippet_chars].rstrip()
        truncated = True

    if truncated and excerpt:
        excerpt = f"{excerpt}\n...[truncated]"

    return excerpt, len(lines), len(excerpt), truncated


def _section_meta(*, input_count: int, output_count: int, cap: int) -> dict[str, Any]:
    dropped_count = max(input_count - output_count, 0)
    return {
        "applied_cap": cap,
        "input_count": input_count,
        "output_count": output_count,
        "truncated": dropped_count > 0,
        "dropped_count": dropped_count,
    }


def _ordered_categories(context: ReleaseContext) -> list[tuple[str, CategoryContext]]:
    category_rank = {name: index for index, name in enumerate(CATEGORY_ORDER)}
    return sorted(
        context.categories.items(),
        key=lambda item: (category_rank.get(item[0], len(CATEGORY_ORDER)), item[0]),
    )


def _truncate_text(text: str, max_chars: int) -> tuple[str, bool]:
    if len(text) <= max_chars:
        return text, False
    return text[:max_chars].rstrip(), True
