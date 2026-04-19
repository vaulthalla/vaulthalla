from __future__ import annotations

import json
from dataclasses import asdict

from tools.release.changelog.models import ReleaseContext


def render_release_changelog(context: ReleaseContext) -> str:
    lines = [f"## {context.version}", ""]

    for name, category in context.categories.items():
        lines.append(f"### {name}")
        lines.append(
            f"Commits: {category.commit_count} | +{category.insertions} / -{category.deletions}"
        )

        for file_change in category.files[:5]:
            lines.append(
                f"- {file_change.path} (score={file_change.score}, "
                f"+{file_change.insertions}/-{file_change.deletions})"
            )
        lines.append("")

    return "\n".join(lines).strip() + "\n"


def render_debug_context(context: ReleaseContext) -> str:
    lines = [
        "=== RELEASE CONTEXT DEBUG ===",
        "",
        f"Version:        {context.version}",
        f"Previous tag:   {context.previous_tag}",
        f"HEAD:           {context.head_sha}",
        f"Commit count:   {context.commit_count}",
        "",
    ]

    for name, category in context.categories.items():
        lines.extend(
            [
                f"--- CATEGORY: {name} ---",
                f"Commits:      {category.commit_count}",
                f"Insertions:   {category.insertions}",
                f"Deletions:    {category.deletions}",
                f"Themes:       {', '.join(category.detected_themes) or 'none'}",
                "",
                "Top Files:",
            ]
        )

        for file_change in category.files[:5]:
            lines.append(
                f"  - {file_change.path} "
                f"(score={file_change.score}, +{file_change.insertions}/-{file_change.deletions}, "
                f"commits={file_change.commit_count}, flags={list(file_change.flags)})"
            )

        lines.append("")
        lines.append("Recent Commits:")
        for commit in category.commits[:5]:
            lines.append(f"  - {commit.subject} ({commit.sha[:7]})")

        lines.append("")
        lines.append("Top Snippets:")
        for snippet in category.snippets[:3]:
            lines.append(f"  - {snippet.path} (score={snippet.score}, reason={snippet.reason})")

        lines.extend(["", ""])

    return "\n".join(lines).rstrip() + "\n"


def render_debug_json(context: ReleaseContext) -> str:
    return json.dumps(asdict(context), indent=2)
