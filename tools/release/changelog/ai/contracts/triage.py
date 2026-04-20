from __future__ import annotations

from dataclasses import dataclass
from typing import Any


AI_TRIAGE_SCHEMA_VERSION = "vaulthalla.release.ai_triage.v1"

AI_TRIAGE_RESPONSE_JSON_SCHEMA: dict[str, Any] = {
    "type": "object",
    "additionalProperties": False,
    "required": ["schema_version", "version", "summary_points", "categories"],
    "properties": {
        "schema_version": {"type": "string", "const": AI_TRIAGE_SCHEMA_VERSION},
        "version": {"type": "string", "minLength": 1, "maxLength": 80},
        "summary_points": {
            "type": "array",
            "minItems": 1,
            "maxItems": 8,
            "items": {"type": "string", "minLength": 1, "maxLength": 260},
        },
        "categories": {
            "type": "array",
            "minItems": 1,
            "maxItems": 10,
            "items": {
                "type": "object",
                "additionalProperties": False,
                "required": [
                    "name",
                    "signal_strength",
                    "priority_rank",
                    "key_points",
                    "important_files",
                    "retained_snippets",
                ],
                "properties": {
                    "name": {"type": "string", "minLength": 1, "maxLength": 60},
                    "signal_strength": {"type": "string", "enum": ["strong", "moderate", "weak"]},
                    "priority_rank": {"type": "integer", "minimum": 1, "maximum": 20},
                    "key_points": {
                        "type": "array",
                        "minItems": 1,
                        "maxItems": 6,
                        "items": {"type": "string", "minLength": 1, "maxLength": 260},
                    },
                    "important_files": {
                        "type": "array",
                        "maxItems": 6,
                        "items": {"type": "string", "minLength": 1, "maxLength": 240},
                    },
                    "retained_snippets": {
                        "type": "array",
                        "maxItems": 4,
                        "items": {"type": "string", "minLength": 1, "maxLength": 420},
                    },
                    "caution_notes": {
                        "type": "array",
                        "maxItems": 4,
                        "items": {"type": "string", "minLength": 1, "maxLength": 240},
                    },
                },
            },
        },
        "dropped_noise": {
            "type": "array",
            "maxItems": 12,
            "items": {"type": "string", "minLength": 1, "maxLength": 260},
        },
        "caution_notes": {
            "type": "array",
            "maxItems": 8,
            "items": {"type": "string", "minLength": 1, "maxLength": 260},
        },
    },
}


@dataclass(frozen=True)
class AITriageCategory:
    name: str
    signal_strength: str
    priority_rank: int
    key_points: tuple[str, ...]
    important_files: tuple[str, ...]
    retained_snippets: tuple[str, ...]
    caution_notes: tuple[str, ...] = ()


@dataclass(frozen=True)
class AITriageResult:
    schema_version: str
    version: str
    summary_points: tuple[str, ...]
    categories: tuple[AITriageCategory, ...]
    dropped_noise: tuple[str, ...] = ()
    caution_notes: tuple[str, ...] = ()


def parse_ai_triage_response(data: Any) -> AITriageResult:
    if not isinstance(data, dict):
        raise ValueError("AI triage response must be a JSON object.")

    schema_version = _read_non_empty_string(data, "schema_version")
    if schema_version != AI_TRIAGE_SCHEMA_VERSION:
        raise ValueError(
            f"`schema_version` must be {AI_TRIAGE_SCHEMA_VERSION!r}, got {schema_version!r}."
        )

    version = _read_non_empty_string(data, "version")
    summary_points = _read_non_empty_string_list(data, "summary_points", required=True)
    categories_raw = data.get("categories")
    if not isinstance(categories_raw, list) or not categories_raw:
        raise ValueError("AI triage response must include non-empty `categories` list.")

    seen_names: set[str] = set()
    seen_ranks: set[int] = set()
    categories: list[AITriageCategory] = []
    for index, category_raw in enumerate(categories_raw):
        if not isinstance(category_raw, dict):
            raise ValueError(f"AI triage category at index {index} must be an object.")

        path = f"categories[{index}]"
        name = _read_non_empty_string(category_raw, "name", path=path)
        if name in seen_names:
            raise ValueError(f"Duplicate triage category name `{name}` is not allowed.")
        seen_names.add(name)

        signal_strength = _read_non_empty_string(category_raw, "signal_strength", path=path)
        if signal_strength not in {"strong", "moderate", "weak"}:
            raise ValueError(
                f"`{path}.signal_strength` must be one of strong|moderate|weak."
            )

        priority_rank_raw = category_raw.get("priority_rank")
        if not isinstance(priority_rank_raw, int) or priority_rank_raw < 1:
            raise ValueError(f"`{path}.priority_rank` must be an integer >= 1.")
        if priority_rank_raw in seen_ranks:
            raise ValueError(f"Duplicate triage priority rank `{priority_rank_raw}` is not allowed.")
        seen_ranks.add(priority_rank_raw)

        key_points = _read_non_empty_string_list(category_raw, "key_points", path=path, required=True)
        important_files = _read_non_empty_string_list(category_raw, "important_files", path=path)
        retained_snippets = _read_non_empty_string_list(category_raw, "retained_snippets", path=path)
        caution_notes = _read_non_empty_string_list(category_raw, "caution_notes", path=path)

        categories.append(
            AITriageCategory(
                name=name,
                signal_strength=signal_strength,
                priority_rank=priority_rank_raw,
                key_points=tuple(key_points),
                important_files=tuple(important_files),
                retained_snippets=tuple(retained_snippets),
                caution_notes=tuple(caution_notes),
            )
        )

    categories.sort(key=lambda item: item.priority_rank)
    return AITriageResult(
        schema_version=schema_version,
        version=version,
        summary_points=tuple(summary_points),
        categories=tuple(categories),
        dropped_noise=tuple(_read_non_empty_string_list(data, "dropped_noise")),
        caution_notes=tuple(_read_non_empty_string_list(data, "caution_notes")),
    )


def ai_triage_result_to_dict(result: AITriageResult) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "schema_version": result.schema_version,
        "version": result.version,
        "summary_points": list(result.summary_points),
        "categories": [
            {
                "name": category.name,
                "signal_strength": category.signal_strength,
                "priority_rank": category.priority_rank,
                "key_points": list(category.key_points),
                "important_files": list(category.important_files),
                "retained_snippets": list(category.retained_snippets),
            }
            for category in result.categories
        ],
    }
    for index, category in enumerate(result.categories):
        if category.caution_notes:
            payload["categories"][index]["caution_notes"] = list(category.caution_notes)
    if result.dropped_noise:
        payload["dropped_noise"] = list(result.dropped_noise)
    if result.caution_notes:
        payload["caution_notes"] = list(result.caution_notes)
    return payload


def build_triage_ir_payload(result: AITriageResult) -> dict[str, Any]:
    """Return compact, deterministic draft-input representation from triage result."""
    return {
        "schema_version": result.schema_version,
        "version": result.version,
        "summary_points": list(result.summary_points),
        "categories": [
            {
                "name": category.name,
                "signal_strength": category.signal_strength,
                "priority_rank": category.priority_rank,
                "key_points": list(category.key_points),
                "important_files": list(category.important_files),
                "retained_snippets": list(category.retained_snippets),
                "caution_notes": list(category.caution_notes),
            }
            for category in result.categories
        ],
        "dropped_noise": list(result.dropped_noise),
        "caution_notes": list(result.caution_notes),
    }


def _read_non_empty_string(obj: dict[str, Any], key: str, path: str | None = None) -> str:
    value = obj.get(key)
    if not isinstance(value, str) or not value.strip():
        prefix = f"{path}." if path else ""
        raise ValueError(f"`{prefix}{key}` must be a non-empty string.")
    return value.strip()


def _read_non_empty_string_list(
    obj: dict[str, Any],
    key: str,
    *,
    path: str | None = None,
    required: bool = False,
) -> list[str]:
    raw = obj.get(key)
    prefix = f"{path}." if path else ""

    if raw is None:
        if required:
            raise ValueError(f"`{prefix}{key}` must be a non-empty list of strings.")
        return []
    if not isinstance(raw, list):
        raise ValueError(f"`{prefix}{key}` must be a list of strings.")
    if required and not raw:
        raise ValueError(f"`{prefix}{key}` must be a non-empty list of strings.")

    normalized: list[str] = []
    for index, value in enumerate(raw):
        if not isinstance(value, str) or not value.strip():
            raise ValueError(f"`{prefix}{key}[{index}]` must be a non-empty string.")
        normalized.append(value.strip())
    return normalized
