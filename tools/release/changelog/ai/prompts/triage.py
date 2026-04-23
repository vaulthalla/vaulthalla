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
        "Emit caution-style notes only when evidence is weak, conflicting, or requires operator verification. "
        "Return JSON only that matches the schema."
    )
    if not hosted_compact_mode:
        return base
    return (
        f"{base} "
        "This is a compression stage: produce the smallest valid high-signal object, "
        "not a comprehensive narrative. "
        "Preserve strongest summary points, clear top-category distinctions, and claim-level evidence anchors."
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
    grounded_claims_max_items = 4 if hosted_compact_mode else 6
    evidence_refs_max_items = 2 if hosted_compact_mode else 4
    compression_directive = (
        "- Compression mode (hosted): prefer shorter arrays and denser phrasing; avoid rationale expansion.\n"
        "- Compression mode (hosted): keep top category distinctions and strongest grounded claims.\n"
        if hosted_compact_mode
        else ""
    )
    return (
        "Build a claim-centric triage intermediate representation from the semantic release payload.\n"
        "Requirements:\n"
        "- Keep only evidence needed for downstream drafting.\n"
        "- Create a category only when at least one concrete supporting item exists.\n"
        "- Rank categories by `priority_rank` (1 is highest).\n"
        f"- When `summary_points` is present, keep it concise and evidence-bound (max {summary_max_items}).\n"
        f"- Keep category `grounded_claims` concise and evidence-bound (max {grounded_claims_max_items}).\n"
        "- Use category `theme` as a compact meaning-first descriptor of what changed.\n"
        f"- Use `evidence_refs` only as short support anchors (max {evidence_refs_max_items}); do not let paths dominate output.\n"
        "- Avoid path-heavy recap and file-list dumping.\n"
        "- Do not paraphrase metadata/churn; focus on concrete behavior/contract changes in the evidence.\n"
        "- `operator_note` is optional and should appear only when evidence is weak, conflicting, or risky.\n"
        "- No filler prose, no intro/outro text, no repetition.\n"
        f"- Keep `categories` short and discriminating (max {category_max_items}).\n"
        f"{compression_directive}"
        "- Required top-level output fields: `schema_version`, `version`, `categories`.\n"
        "- Optional top-level fields: `summary_points`, `operator_note`.\n"
        f"- Set `schema_version` exactly to `{AI_TRIAGE_SCHEMA_VERSION}`.\n"
        "- Required category fields: `name`, `signal_strength`, `priority_rank`, `theme`, `grounded_claims`.\n"
        "- Optional category fields: `evidence_refs`, `operator_note`.\n"
        "- Never emit empty placeholders in arrays (no `\"\"`, whitespace-only items, nulls, or other non-string placeholders); omit blank items.\n"
        "- Return JSON only.\n\n"
        "Semantic payload (compact projection):\n"
        f"{payload_json}"
    )


def _build_compact_payload_projection(
    payload: dict[str, Any],
    *,
    hosted_compact_mode: bool,
) -> dict[str, Any]:
    categories_raw = _as_sequence(payload.get("categories"))
    notes_raw = payload.get("notes")
    category_limit = 6 if hosted_compact_mode else 10
    notes_limit = 4 if hosted_compact_mode else 8
    commit_limit = 2 if hosted_compact_mode else 3
    hunk_limit = 2 if hosted_compact_mode else 3
    files_limit = 1 if hosted_compact_mode else 2

    compact: dict[str, Any] = {
        "schema_version": payload.get("schema_version"),
        "version": _read_nested_string(payload, "version"),
        "previous_tag": _read_nested_string(payload, "previous_tag"),
        "head_sha": _read_nested_string(payload, "head_sha"),
        "commit_count": _read_nested_int(payload, "commit_count"),
        "categories": [],
        "notes": _normalize_string_list(notes_raw, limit=notes_limit),
    }

    categories: list[dict[str, Any]] = []
    for category in categories_raw[:category_limit]:
        if not isinstance(category, dict):
            continue
        categories.append(
            {
                "name": _read_nested_string(category, "name"),
                "signal_strength": _read_nested_string(category, "signal_strength"),
                "summary_hint": _truncate(_read_nested_string(category, "summary_hint"), limit=220),
                "key_commits": _normalize_string_list(category.get("key_commits"), limit=commit_limit),
                "semantic_hunks": _compact_semantic_hunks(
                    category.get("semantic_hunks"),
                    hosted_compact_mode=hosted_compact_mode,
                    limit=hunk_limit,
                ),
                "supporting_files": _normalize_string_list(
                    category.get("supporting_files"),
                    limit=files_limit,
                ),
            }
        )
    compact["categories"] = categories
    return compact


def _compact_semantic_hunks(
    raw: Any,
    *,
    hosted_compact_mode: bool,
    limit: int,
) -> list[dict[str, Any]]:
    items = _as_sequence(raw)
    compact: list[dict[str, Any]] = []
    excerpt_limit = 180 if hosted_compact_mode else 340
    for item in items:
        if not isinstance(item, dict):
            continue
        compact.append(
            {
                "kind": _read_nested_string(item, "kind"),
                "why_selected": _truncate(_read_nested_string(item, "why_selected"), limit=180),
                "excerpt": _truncate(_read_nested_string(item, "excerpt"), limit=excerpt_limit),
                "evidence_ref": _read_nested_string(item, "path"),
            }
        )
        if len(compact) >= limit:
            break
    return compact


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


def _normalize_string_list(raw: Any, *, limit: int) -> list[str]:
    items = _as_sequence(raw)
    out: list[str] = []
    for value in items:
        if not isinstance(value, str):
            continue
        trimmed = value.strip()
        if not trimmed:
            continue
        out.append(_truncate(trimmed, limit=180) or trimmed)
        if len(out) >= limit:
            break
    return out


def _truncate(value: str | None, *, limit: int) -> str | None:
    if value is None:
        return None
    if len(value) <= limit:
        return value
    return value[: limit - 3].rstrip() + "..."


def _as_sequence(raw: Any) -> list[Any]:
    if isinstance(raw, list):
        return raw
    if isinstance(raw, tuple):
        return list(raw)
    return []
