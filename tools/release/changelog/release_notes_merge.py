from __future__ import annotations

import re

GENERATED_RELEASE_NOTES_START = "<!-- vaulthalla-generated-release-notes:start -->"
GENERATED_RELEASE_NOTES_END = "<!-- vaulthalla-generated-release-notes:end -->"

_WHAT_CHANGED_RE = re.compile(r"(?im)^##\s+What's Changed\s*$")
_GENERATED_BLOCK_RE = re.compile(
    rf"(?:[ \t]*\n)*{re.escape(GENERATED_RELEASE_NOTES_START)}[\s\S]*?"
    rf"{re.escape(GENERATED_RELEASE_NOTES_END)}(?:[ \t]*\n)*"
)


def merge_release_notes_body(
    existing_body: str,
    generated_notes: str,
    *,
    tag: str | None = None,
) -> str:
    """Merge generated release notes into a GitHub Release body.

    User-provided prose is preserved outside hidden generated-note markers. When
    user prose exists, only the generated "What's Changed" section is appended.
    """
    del tag
    user_body = _remove_generated_blocks(existing_body)
    generated = generated_notes.strip()

    if user_body:
        generated_section = _extract_or_wrap_whats_changed(generated)
        return _join_body_and_generated_block(user_body, generated_section)

    standalone = _ensure_standalone_generated_notes(generated)
    return _generated_block(standalone)


def _remove_generated_blocks(body: str) -> str:
    stripped = (body or "").strip()
    if not stripped:
        return ""
    without_blocks = _GENERATED_BLOCK_RE.sub("\n\n", stripped)
    return without_blocks.strip()


def _extract_or_wrap_whats_changed(generated_notes: str) -> str:
    match = _WHAT_CHANGED_RE.search(generated_notes)
    if match is not None:
        return generated_notes[match.start() :].strip()
    return f"## What's Changed\n\n{generated_notes}".strip()


def _ensure_standalone_generated_notes(generated_notes: str) -> str:
    match = _WHAT_CHANGED_RE.search(generated_notes)
    if match is None:
        return f"Generated release notes for this Vaulthalla release.\n\n## What's Changed\n\n{generated_notes}".strip()
    if generated_notes[: match.start()].strip():
        return generated_notes.strip()
    return f"Generated release notes for this Vaulthalla release.\n\n{generated_notes}".strip()


def _join_body_and_generated_block(user_body: str, generated_section: str) -> str:
    return f"{user_body.strip()}\n\n{_generated_block(generated_section)}"


def _generated_block(content: str) -> str:
    return f"{GENERATED_RELEASE_NOTES_START}\n{content.strip()}\n{GENERATED_RELEASE_NOTES_END}\n"
