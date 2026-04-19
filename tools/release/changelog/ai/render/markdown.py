from __future__ import annotations

from tools.release.changelog.ai.contracts.draft import AIDraftResult


def render_draft_markdown(draft: AIDraftResult) -> str:
    lines = [f"# {draft.title}", "", draft.summary, ""]

    for section in draft.sections:
        lines.append(f"## {section.category.title()}")
        lines.append(section.overview)
        lines.append("")
        for bullet in section.bullets:
            lines.append(f"- {bullet}")
        lines.append("")

    if draft.notes:
        lines.append("## Notes")
        for note in draft.notes:
            lines.append(f"- {note}")
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"
