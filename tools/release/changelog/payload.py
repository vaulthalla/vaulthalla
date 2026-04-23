from __future__ import annotations

from collections import Counter
from dataclasses import asdict, dataclass, field
import json
import re
from typing import Any

from tools.release.changelog.categorize import CATEGORY_ORDER
from tools.release.changelog.models import (
    CategoryContext,
    CategorySemanticPacket,
    CommitInfo,
    DiffSnippet,
    FileChange,
    ReleaseContext,
    SemanticHunk,
    SemanticReleasePayload,
)

AI_PAYLOAD_SCHEMA_VERSION = "vaulthalla.release.ai_payload.v1"
AI_SEMANTIC_PAYLOAD_SCHEMA_VERSION = "vaulthalla.release.semantic_payload.v1"

_SEMANTIC_KIND_PRIORITY: dict[str, int] = {
    "api-contract": 10,
    "command-surface": 9,
    "schema-change": 8,
    "prompt-contract": 8,
    "config-surface": 7,
    "error-handling": 7,
    "output-artifact": 7,
    "filesystem-lifecycle": 6,
    "packaging-script": 6,
    "tests-contract": 4,
    "implementation-change": 3,
}

_SEMANTIC_KIND_WHY_TEXT: dict[str, str] = {
    "api-contract": "captures API request/response contract behavior changes",
    "command-surface": "captures command-line surface changes",
    "schema-change": "captures structured schema field changes",
    "prompt-contract": "captures prompt/instruction contract changes",
    "config-surface": "captures configuration surface changes",
    "error-handling": "captures validation or error-handling behavior changes",
    "output-artifact": "captures release artifact output path/format changes",
    "filesystem-lifecycle": "captures lifecycle or teardown behavior changes",
    "packaging-script": "captures packaging/install script behavior changes",
    "tests-contract": "captures test assertions that define behavior contracts",
    "implementation-change": "captures implementation flow changes with behavioral impact",
}

_SEMANTIC_KIND_TOPICS: dict[str, str] = {
    "api-contract": "API request/response contract handling",
    "command-surface": "command and CLI behavior",
    "schema-change": "schema and structured-output fields",
    "prompt-contract": "prompt and generation contract text",
    "config-surface": "configuration keys and defaults",
    "error-handling": "validation and error handling",
    "output-artifact": "release artifact generation paths",
    "filesystem-lifecycle": "lifecycle and teardown behavior",
    "packaging-script": "packaging/install script behavior",
    "tests-contract": "behavioral contract tests",
    "implementation-change": "core implementation flow",
}

_SEMANTIC_THEME_TOPICS: dict[str, str] = {
    "configuration": "configuration keys and defaults",
    "database": "database integration behavior",
    "packaging": "packaging/install script behavior",
    "release-automation": "release artifact generation paths",
    "service-management": "service lifecycle handling",
}

_CATEGORY_SUMMARY_PREFIX: dict[str, str] = {
    "debian": "Packaging and Debian updates",
    "tools": "Release tooling updates",
    "deploy": "Deployment updates",
    "web": "Web application updates",
    "core": "Core runtime updates",
    "meta": "Project metadata updates",
}

_SEMANTIC_TOPIC_PRIORITY: dict[str, int] = {
    "API request/response contract handling": 1,
    "command and CLI behavior": 2,
    "schema and structured-output fields": 3,
    "prompt and generation contract text": 4,
    "configuration keys and defaults": 5,
    "validation and error handling": 6,
    "release artifact generation paths": 7,
    "lifecycle and teardown behavior": 8,
    "packaging/install script behavior": 9,
    "database integration behavior": 10,
    "service lifecycle handling": 11,
    "behavioral contract tests": 12,
    "core implementation flow": 13,
}

_SEMANTIC_SIGNATURE_RE = re.compile(
    r"\b(def|class|interface|enum|struct|function|async\s+def)\b|--[a-z0-9][a-z0-9_-]*",
    re.IGNORECASE,
)
_SEMANTIC_CONFIG_KEY_RE = re.compile(r"^[+-]\s*[\"']?[a-zA-Z_][a-zA-Z0-9_.-]*[\"']?\s*[:=]")
_SEMANTIC_IDENTIFIER_RE = re.compile(
    r"\b(def|class|interface|enum|struct|function|async\s+def)\s+([A-Za-z_][A-Za-z0-9_]*)"
)
_SEMANTIC_CLI_FLAG_RE = re.compile(r"--[a-z0-9][a-z0-9_-]*")
_SEMANTIC_ENDPOINT_RE = re.compile(r"/v1/[a-z0-9/_-]+")
_SEMANTIC_CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")


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


def build_semantic_ai_payload(
    context: ReleaseContext,
    config: PayloadBuildConfig | None = None,
) -> dict[str, Any]:
    """Build deterministic semantic-first payload in parallel to forensic payload."""
    cfg = config if config is not None else PayloadBuildConfig()
    ordered_categories = _ordered_categories(context)
    selected: list[CategorySemanticPacket] = []

    for name, category in ordered_categories:
        signal_strength = classify_signal_strength(category)
        if signal_strength == "weak" and not cfg.include_weak_categories:
            continue
        selected.append(
            _build_category_semantic_packet(
                name=name,
                category=category,
                signal_strength=signal_strength,
                limits=cfg.limits,
            )
        )
        if cfg.limits.max_categories is not None and len(selected) >= cfg.limits.max_categories:
            break

    notes = tuple(context.cross_cutting_notes)
    semantic = SemanticReleasePayload(
        schema_version=AI_SEMANTIC_PAYLOAD_SCHEMA_VERSION,
        version=context.version,
        previous_tag=context.previous_tag,
        head_sha=context.head_sha,
        commit_count=context.commit_count,
        categories=tuple(selected),
        notes=notes,
    )
    return asdict(semantic)


def render_semantic_ai_payload_json(payload: dict[str, Any]) -> str:
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


def _build_category_semantic_packet(
    *,
    name: str,
    category: CategoryContext,
    signal_strength: str,
    limits: PayloadLimits,
) -> CategorySemanticPacket:
    selected_semantic_snippets = _select_semantic_snippets(category.snippets, limits)
    commit_subjects = tuple(
        _truncate_text(commit.subject.strip(), limits.max_commit_subject_chars)[0]
        for commit in category.commits[: limits.max_commits_per_category]
    )
    supporting_files = _build_semantic_supporting_files(
        category,
        selected_semantic_snippets,
        limits,
    )
    semantic_hunks: list[SemanticHunk] = []
    for snippet, kind in selected_semantic_snippets:
        excerpt = _build_semantic_excerpt(snippet.patch, limits)
        semantic_hunks.append(
            SemanticHunk(
                path=snippet.path,
                kind=kind,
                why_selected=_build_semantic_why_selected(snippet, kind),
                excerpt=excerpt,
            )
        )

    caution_notes: tuple[str, ...] = ()
    if signal_strength == "weak":
        caution_notes = ("Low-signal category with limited concrete evidence.",)

    return CategorySemanticPacket(
        name=name,
        signal_strength=signal_strength,
        summary_hint=_build_category_summary_hint(name, category, semantic_hunks),
        key_commits=commit_subjects,
        supporting_files=supporting_files,
        semantic_hunks=tuple(semantic_hunks),
        caution_notes=caution_notes,
    )


def _build_category_summary_hint(
    category_name: str,
    category: CategoryContext,
    semantic_hunks: list[SemanticHunk],
) -> str:
    prefix = _CATEGORY_SUMMARY_PREFIX.get(category_name, f"{category_name.title()} updates")
    topics: Counter[str] = Counter()

    for hunk in semantic_hunks:
        topic = _SEMANTIC_KIND_TOPICS.get(hunk.kind)
        if topic:
            topics[topic] += 2

    for theme in category.detected_themes:
        topic = _SEMANTIC_THEME_TOPICS.get(theme)
        if topic:
            topics[topic] += 1

    if topics:
        ranked_topics = sorted(
            topics.items(),
            key=lambda item: (
                -item[1],
                _SEMANTIC_TOPIC_PRIORITY.get(item[0], 999),
                item[0],
            ),
        )
        top_topics = [topic for topic, _weight in ranked_topics[:3]]
        return f"{prefix} covering {_join_human_list(top_topics)}."

    return f"{prefix} with limited high-signal semantic evidence."


def _join_human_list(items: list[str]) -> str:
    if not items:
        return ""
    if len(items) == 1:
        return items[0]
    if len(items) == 2:
        return f"{items[0]} and {items[1]}"
    return ", ".join(items[:-1]) + f", and {items[-1]}"


def _build_semantic_supporting_files(
    category: CategoryContext,
    selected_snippets: list[tuple[DiffSnippet, str]],
    limits: PayloadLimits,
) -> tuple[str, ...]:
    cap = max(1, min(limits.max_files_per_category, 2))
    selected: list[str] = []
    seen: set[str] = set()

    for snippet, _kind in selected_snippets:
        if snippet.path in seen:
            continue
        selected.append(snippet.path)
        seen.add(snippet.path)
        if len(selected) >= cap:
            return tuple(selected)

    for file_change in category.files:
        if file_change.path in seen:
            continue
        selected.append(file_change.path)
        seen.add(file_change.path)
        if len(selected) >= cap:
            break

    return tuple(selected)


def _select_semantic_snippets(
    snippets: list[DiffSnippet],
    limits: PayloadLimits,
) -> list[tuple[DiffSnippet, str]]:
    if not snippets:
        return []

    ranked: list[tuple[float, int, DiffSnippet, str]] = []
    for index, snippet in enumerate(snippets):
        kind = _classify_semantic_hunk_kind(snippet)
        semantic_score = _score_semantic_snippet(snippet, kind)
        ranked.append((semantic_score, index, snippet, kind))

    ranked.sort(key=lambda item: (-item[0], item[1], item[2].path))
    selected = ranked[: limits.max_snippets_per_category]
    return [(snippet, kind) for _score, _index, snippet, kind in selected]


def _score_semantic_snippet(snippet: DiffSnippet, kind: str) -> float:
    lines = _semantic_changed_lines(snippet.patch)
    meaningful_lines = [line for line in lines if not _is_noise_change_line(line)]
    score = float(_SEMANTIC_KIND_PRIORITY.get(kind, 1) * 10)
    score += min(len(meaningful_lines), 8) * 1.8

    lower = "\n".join(meaningful_lines).lower()
    if _SEMANTIC_SIGNATURE_RE.search(lower):
        score += 4.0
    if any(token in lower for token in ("if ", "elif ", "else:", "try:", "except", "raise", "error", "validate")):
        score += 3.0
    if _SEMANTIC_CONFIG_KEY_RE.search("\n".join(lines)):
        score += 2.0
    if not meaningful_lines and lines:
        score -= 10.0
    if _is_import_only_hunk(lines):
        score -= 8.0
    if len(lines) > 32:
        score -= 2.5
    if len(lines) > 64:
        score -= 2.5

    # Keep original snippet score as a tie-break signal, not the primary semantic selector.
    score += min(snippet.score, 20.0) / 20.0
    return round(score, 4)


def _classify_semantic_hunk_kind(snippet: DiffSnippet) -> str:
    lower_path = snippet.path.lower()
    lower_patch = snippet.patch.lower()
    flags = {flag.lower() for flag in snippet.flags}
    merged = f"{lower_path}\n{lower_patch}\n{' '.join(sorted(flags))}"

    if "/tests/" in f"/{lower_path}" or lower_path.startswith("test_") or "/test_" in lower_path:
        return "tests-contract"
    if "/prompts/" in f"/{lower_path}" or "prompt" in merged:
        return "prompt-contract"
    if any(token in merged for token in (".changelog_scratch", "changelog.release.md", "release_notes.md")):
        return "output-artifact"
    if lower_path.endswith("cli.py") or "/cli/" in f"/{lower_path}" or any(
        token in merged for token in ("argparse", "subparsers", "add_parser(", "--")
    ):
        return "command-surface"
    if (
        snippet.category == "debian"
        or lower_path.startswith("debian/")
        or "packaging" in flags
        or "install-script" in flags
    ):
        return "packaging-script"
    if "schema" in merged or "json_schema" in merged or "schema_version" in merged:
        return "schema-change"
    if any(
        token in merged
        for token in (
            "/v1/responses",
            "/v1/chat/completions",
            "openai",
            "instructions",
            "messages",
            "payload",
            "reasoning",
            "temperature",
            "provider",
            "response_format",
            "base_url",
        )
    ):
        return "api-contract"
    if (
        "config" in flags
        or lower_path.endswith((".yml", ".yaml", ".toml", ".ini"))
        or _SEMANTIC_CONFIG_KEY_RE.search(snippet.patch) is not None
    ):
        return "config-surface"
    if any(token in merged for token in ("error", "exception", "raise", "retry", "validate", "invalid")):
        return "error-handling"
    if any(
        token in merged
        for token in ("teardown", "cleanup", "shutdown", "lifecycle", "mount", "unmount", "unlink", "remove")
    ) or "filesystem" in flags:
        return "filesystem-lifecycle"
    return "implementation-change"


def _build_semantic_excerpt(patch: str, limits: PayloadLimits) -> str:
    max_lines = min(limits.max_snippet_lines, 10)
    max_chars = min(limits.max_snippet_chars, 500)
    raw_lines = patch.strip().splitlines() if patch.strip() else []
    if not raw_lines:
        return ""

    header: str | None = raw_lines[0] if raw_lines and raw_lines[0].startswith("@@") else None
    changed_lines = _semantic_changed_lines(patch)
    meaningful_lines = [line for line in changed_lines if not _is_noise_change_line(line)]
    selected_body = meaningful_lines if meaningful_lines else changed_lines
    body_limit = max_lines - (1 if header is not None else 0)
    body_limit = max(body_limit, 1)
    selected_lines = selected_body[:body_limit]

    excerpt_lines: list[str] = []
    if header is not None:
        excerpt_lines.append(header)
    excerpt_lines.extend(selected_lines)

    excerpt = "\n".join(excerpt_lines)
    truncated = len(selected_body) > len(selected_lines)

    if len(excerpt) > max_chars:
        excerpt = excerpt[:max_chars].rstrip()
        truncated = True

    if truncated and excerpt:
        excerpt = f"{excerpt}\n...[truncated]"
    return excerpt


def _build_semantic_why_selected(snippet: DiffSnippet, kind: str) -> str:
    reason = _SEMANTIC_KIND_WHY_TEXT.get(kind, "captures meaningful behavior changes")
    anchors = _extract_semantic_anchors(snippet)
    if not anchors:
        return f"{reason}."
    anchor_text = ", ".join(f"`{anchor}`" for anchor in anchors)
    return f"{reason}; anchors: {anchor_text}."


def _extract_semantic_anchors(snippet: DiffSnippet) -> tuple[str, ...]:
    anchors: list[str] = []
    seen: set[str] = set()
    for line in _semantic_changed_lines(snippet.patch):
        raw = line[1:].strip() if line.startswith(("+", "-")) else line.strip()
        if not raw:
            continue

        identifier_match = _SEMANTIC_IDENTIFIER_RE.search(raw)
        if identifier_match is not None:
            _add_semantic_anchor(anchors, seen, identifier_match.group(2))

        for flag in _SEMANTIC_CLI_FLAG_RE.findall(raw):
            _add_semantic_anchor(anchors, seen, flag)

        for endpoint in _SEMANTIC_ENDPOINT_RE.findall(raw):
            _add_semantic_anchor(anchors, seen, endpoint)

        config_match = re.match(r"^[\"']?([a-zA-Z_][a-zA-Z0-9_.-]*)[\"']?\s*[:=]", raw)
        if config_match is not None:
            _add_semantic_anchor(anchors, seen, config_match.group(1))

        for call in _SEMANTIC_CALL_RE.findall(raw):
            if call in {"if", "for", "while", "switch", "return"}:
                continue
            _add_semantic_anchor(anchors, seen, call)

        for token in (
            "reasoning",
            "temperature",
            "instructions",
            "messages",
            "input",
            "output",
            "response",
            "request",
            "schema_version",
            "release_notes.md",
            "changelog.semantic_payload.json",
            "debian/changelog",
        ):
            if token in raw:
                _add_semantic_anchor(anchors, seen, token)

        if len(anchors) >= 2:
            break

    return tuple(anchors[:2])


def _add_semantic_anchor(anchors: list[str], seen: set[str], candidate: str) -> None:
    cleaned = candidate.strip("`\"' ")
    if not cleaned:
        return
    if cleaned in seen:
        return
    anchors.append(cleaned)
    seen.add(cleaned)


def _semantic_changed_lines(patch: str) -> list[str]:
    lines: list[str] = []
    for line in patch.splitlines():
        if line.startswith("+++ ") or line.startswith("--- ") or line.startswith("@@"):
            continue
        if line.startswith("+") or line.startswith("-"):
            lines.append(line)
    return lines


def _is_import_only_hunk(lines: list[str]) -> bool:
    filtered = [line for line in lines if line[1:].strip()]  # account for +/- prefix
    if not filtered:
        return False
    return all(_is_import_like_line(line) for line in filtered)


def _is_import_like_line(line: str) -> bool:
    payload = line[1:].strip() if line.startswith(("+", "-")) else line.strip()
    lower = payload.lower()
    return lower.startswith(("import ", "from ", "#include ", "using ", "use "))


def _is_noise_change_line(line: str) -> bool:
    payload = line[1:].strip() if line.startswith(("+", "-")) else line.strip()
    if not payload:
        return True
    if _is_import_like_line(line):
        return True
    if payload.startswith(("//", "/*", "*", "#")):
        return True
    if re.fullmatch(r"[{}\[\](),.;:]+", payload):
        return True
    return False


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
