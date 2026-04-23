from __future__ import annotations

import json
from typing import Any

from tools.release.changelog.ai.contracts.triage import AI_TRIAGE_SCHEMA_VERSION


def build_triage_system_prompt(*, hosted_compact_mode: bool = False) -> str:
    base = (
        "You are a deterministic release triage classifier for Vaulthalla. "
        "Use only explicit input evidence. "
        "Never invent features, fixes, behavior, impact, or categories. "
        "Do not use speculative wording: likely, suggests, appears to, may indicate, could be interpreted as. "
        "If evidence is weak, state that briefly in caution fields. "
        "Return JSON only that matches the schema."
    )
    if not hosted_compact_mode:
        return base
    return (
        f"{base} "
        "This is a compression stage: produce the smallest valid high-signal object, "
        "not a comprehensive narrative. "
        "Preserve the strongest summary points and clear top-category distinctions for downstream drafting."
    )


def build_triage_user_prompt(
    payload: dict[str, Any],
    *,
    hosted_compact_mode: bool = False,
) -> str:
    payload_json = json.dumps(
        _build_compact_payload_projection(payload, hosted_compact_mode=hosted_compact_mode),
        indent=2,
        sort_keys=False,
    )
    summary_max_items = 4 if hosted_compact_mode else 8
    category_max_items = 5 if hosted_compact_mode else 10
    key_points_max_items = 3 if hosted_compact_mode else 6
    important_files_max_items = 3 if hosted_compact_mode else 6
    retained_snippets_max_items = 1 if hosted_compact_mode else 4
    dropped_noise_max_items = 6 if hosted_compact_mode else 12
    caution_notes_max_items = 4 if hosted_compact_mode else 8
    category_caution_notes_max_items = 2 if hosted_compact_mode else 4
    compression_directive = (
        "- Compression mode (hosted): prefer shorter arrays and denser phrasing; do not expand rationale text.\n"
        "- Compression mode (hosted): retain enough signal for drafting by preserving strongest summary points and top category distinctions.\n"
        if hosted_compact_mode
        else ""
    )
    return (
        "Build a compact triage intermediate representation from the release payload.\n"
        "Requirements:\n"
        "- Keep only evidence required for downstream drafting.\n"
        "- Create a category only when at least one concrete supporting item exists.\n"
        "- Rank categories by `priority_rank` (1 is highest).\n"
        f"- Keep `summary_points` concise and evidence-bound (max {summary_max_items}).\n"
        f"- Keep category `key_points` concise and evidence-bound (max {key_points_max_items}).\n"
        "- No filler prose, no intro/outro text, no repetition.\n"
        f"- Keep `categories` short and discriminating (max {category_max_items}).\n"
        f"- Keep `important_files` to most relevant paths only (max {important_files_max_items}).\n"
        f"- Keep `retained_snippets` short path-like identifiers only (max {retained_snippets_max_items}); no rationale prose.\n"
        f"- Record dropped low-signal details in `dropped_noise` (max {dropped_noise_max_items}).\n"
        f"- Keep top-level `caution_notes` short and sparse (max {caution_notes_max_items}).\n"
        f"- Keep category `caution_notes` short and sparse (max {category_caution_notes_max_items}).\n"
        "- Use `[]` for caution/noise arrays when there is no concrete signal.\n"
        f"{compression_directive}"
        "- Required top-level output fields: `schema_version`, `version`, `summary_points`, `categories`, "
        "`dropped_noise`, `caution_notes`.\n"
        f"- Set `schema_version` exactly to `{AI_TRIAGE_SCHEMA_VERSION}`.\n"
        "- Required category fields: `name`, `signal_strength`, `priority_rank`, `key_points`, "
        "`important_files`, `retained_snippets`, `caution_notes`.\n"
        "- Never emit empty placeholders in arrays (no `\"\"`, whitespace-only items, nulls, or other non-string placeholders); omit blank items.\n"
        "- Return JSON only.\n\n"
        "Release payload (compact projection):\n"
        f"{payload_json}"
    )


def _build_compact_payload_projection(
    payload: dict[str, Any],
    *,
    hosted_compact_mode: bool,
) -> dict[str, Any]:
    metadata = payload.get("metadata")
    generation = payload.get("generation")
    categories_raw = payload.get("categories")
    notes_raw = payload.get("notes")
    selected_category_order_limit = 12 if hosted_compact_mode else 20
    category_limit = 8 if hosted_compact_mode else 10
    notes_limit = 8 if hosted_compact_mode else 12

    compact: dict[str, Any] = {
        "schema_version": payload.get("schema_version"),
        "metadata": {
            "version": _read_nested_string(metadata, "version"),
            "previous_tag": _read_nested_string(metadata, "previous_tag"),
            "head_sha": _read_nested_string(metadata, "head_sha"),
            "commit_count": _read_nested_int(metadata, "commit_count"),
        },
        "generation": {
            "selected_category_order": _read_nested_list(
                generation,
                "selected_category_order",
                limit=selected_category_order_limit,
            ),
            "truncation": {
                "any_truncation": _read_nested_bool(generation, "truncation", "any_truncation"),
                "categories_with_evidence_truncated": _read_nested_list(
                    _read_nested_mapping(generation, "truncation"),
                    "categories_with_evidence_truncated",
                    limit=selected_category_order_limit,
                ),
            },
        },
        "categories": [],
        "notes": _normalize_string_list(notes_raw, limit=notes_limit),
    }

    categories: list[dict[str, Any]] = []
    if isinstance(categories_raw, list):
        for category in categories_raw[:category_limit]:
            if not isinstance(category, dict):
                continue
            summary = category.get("summary")
            evidence = category.get("evidence")
            categories.append(
                {
                    "name": _read_nested_string(category, "name"),
                    "summary": {
                        "signal_strength": _read_nested_string(summary, "signal_strength"),
                        "commit_count": _read_nested_int(summary, "commit_count"),
                        "file_count": _read_nested_int(summary, "file_count"),
                        "snippet_count": _read_nested_int(summary, "snippet_count"),
                        "insertions": _read_nested_int(summary, "insertions"),
                        "deletions": _read_nested_int(summary, "deletions"),
                        "themes": _read_nested_list(summary, "themes", limit=6 if hosted_compact_mode else 10),
                    },
                    "evidence": {
                        "top_commits": _compact_commits(
                            _read_nested_list(evidence, "top_commits", limit=3),
                            hosted_compact_mode=hosted_compact_mode,
                        ),
                        "top_files": _compact_files(
                            _read_nested_list(evidence, "top_files", limit=3),
                            hosted_compact_mode=hosted_compact_mode,
                        ),
                        "top_snippet_paths": _compact_snippet_paths(
                            _read_nested_list(evidence, "top_snippets", limit=2),
                            hosted_compact_mode=hosted_compact_mode,
                        ),
                    },
                }
            )
    compact["categories"] = categories
    return compact


def _compact_commits(items: list[Any], *, hosted_compact_mode: bool) -> list[dict[str, Any]]:
    subject_limit = 96 if hosted_compact_mode else 120
    compact: list[dict[str, Any]] = []
    for item in items:
        if not isinstance(item, dict):
            continue
        compact.append(
            {
                "sha": _read_nested_string(item, "sha"),
                "subject": _truncate(_read_nested_string(item, "subject"), limit=subject_limit),
            }
        )
    return compact


def _compact_files(items: list[Any], *, hosted_compact_mode: bool) -> list[dict[str, Any]]:
    compact: list[dict[str, Any]] = []
    for item in items:
        if not isinstance(item, dict):
            continue
        if hosted_compact_mode:
            compact.append({"path": _read_nested_string(item, "path")})
        else:
            compact.append(
                {
                    "path": _read_nested_string(item, "path"),
                    "insertions": _read_nested_int(item, "insertions"),
                    "deletions": _read_nested_int(item, "deletions"),
                    "flags": _normalize_string_list(item.get("flags"), limit=6),
                }
            )
    return compact


def _compact_snippet_paths(items: list[Any], *, hosted_compact_mode: bool) -> list[str]:
    path_limit = 1 if hosted_compact_mode else 2
    compact: list[str] = []
    for item in items:
        if not isinstance(item, dict):
            continue
        path = _read_nested_string(item, "path")
        if path is None:
            continue
        compact.append(path)
        if len(compact) >= path_limit:
            break
    return compact


def _read_nested_mapping(obj: Any, key: str) -> dict[str, Any] | None:
    if not isinstance(obj, dict):
        return None
    value = obj.get(key)
    return value if isinstance(value, dict) else None


def _read_nested_string(obj: Any, key: str) -> str | None:
    if not isinstance(obj, dict):
        return None
    value = obj.get(key)
    if not isinstance(value, str):
        return None
    trimmed = value.strip()
    return trimmed or None


def _read_nested_int(obj: Any, key: str) -> int | None:
    if not isinstance(obj, dict):
        return None
    value = obj.get(key)
    if isinstance(value, bool) or not isinstance(value, int):
        return None
    return value


def _read_nested_bool(obj: Any, outer_key: str, inner_key: str) -> bool | None:
    outer = _read_nested_mapping(obj, outer_key)
    if not isinstance(outer, dict):
        return None
    value = outer.get(inner_key)
    return value if isinstance(value, bool) else None


def _read_nested_list(obj: Any, key: str, *, limit: int) -> list[Any]:
    if not isinstance(obj, dict):
        return []
    value = obj.get(key)
    if not isinstance(value, list):
        return []
    return value[:limit]


def _normalize_string_list(raw: Any, *, limit: int) -> list[str]:
    if not isinstance(raw, list):
        return []
    out: list[str] = []
    for value in raw:
        if not isinstance(value, str):
            continue
        trimmed = value.strip()
        if not trimmed:
            continue
        out.append(_truncate(trimmed, limit=160))
        if len(out) >= limit:
            break
    return out


def _truncate(value: str | None, *, limit: int) -> str | None:
    if value is None:
        return None
    if len(value) <= limit:
        return value
    return value[: limit - 3].rstrip() + "..."
