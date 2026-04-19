from __future__ import annotations

import argparse
import sys
from pathlib import Path

from tools.release.changelog.ai.draft_mini import (
    generate_mini_draft_from_payload,
    render_ai_draft_json,
    render_ai_draft_markdown,
)
from tools.release.changelog.ai.openai_client import DEFAULT_OPENAI_MINI_MODEL
from tools.release.changelog.context_builder import build_release_context
from tools.release.changelog.payload import build_ai_payload, render_ai_payload_json
from tools.release.changelog.render_raw import render_debug_json, render_release_changelog
from tools.release.version.adapters.version_file import read_version_file
from tools.release.version.models import Version
from tools.release.version.sync import apply_version_update, resolve_debian_revision
from tools.release.version.validate import (
    build_sync_required_message,
    get_release_state,
    require_release_files,
    require_synced_release_state,
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="python -m tools.release",
        description="Vaulthalla release and packaging tooling.",
    )

    parser.add_argument(
        "--repo-root",
        default=".",
        help="Path to the repository root (default: current directory).",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    check_parser = subparsers.add_parser(
        "check",
        help="Validate release file versions and repo release state.",
    )
    check_parser.set_defaults(func=cmd_check)

    sync_parser = subparsers.add_parser(
        "sync",
        help="Synchronize managed release files to the canonical VERSION file.",
    )
    sync_parser.add_argument(
        "--debian-revision",
        type=int,
        default=None,
        help="Optional Debian revision override. Defaults to existing revision or 1.",
    )
    sync_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would change without writing files.",
    )
    sync_parser.set_defaults(func=cmd_sync)

    set_version_parser = subparsers.add_parser(
        "set-version",
        help="Set the application version across release-managed files.",
    )
    set_version_parser.add_argument("version", help="Semantic version to set (e.g. 0.3.0).")
    set_version_parser.add_argument(
        "--debian-revision",
        type=int,
        default=1,
        help="Debian package revision to write (default: 1).",
    )
    set_version_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would change without writing files.",
    )
    set_version_parser.set_defaults(func=cmd_set_version)

    bump_parser = subparsers.add_parser(
        "bump",
        help="Bump the current semantic version.",
    )
    bump_parser.add_argument(
        "part",
        choices=("major", "minor", "patch"),
        help="Version component to bump.",
    )
    bump_parser.add_argument(
        "--debian-revision",
        type=int,
        default=1,
        help="Debian package revision to write after bump (default: 1).",
    )
    bump_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would change without writing files.",
    )
    bump_parser.set_defaults(func=cmd_bump)

    changelog_parser = subparsers.add_parser(
        "changelog",
        help="Generate changelog artifacts from git history.",
    )
    changelog_subparsers = changelog_parser.add_subparsers(dest="changelog_command", required=True)

    changelog_draft_parser = changelog_subparsers.add_parser(
        "draft",
        help="Render a changelog draft from the current release context.",
    )
    changelog_draft_parser.add_argument(
        "--format",
        choices=("raw", "json"),
        default="raw",
        help="Output format (default: raw).",
    )
    changelog_draft_parser.add_argument(
        "--since-tag",
        default=None,
        help="Optional tag to use as lower bound (overrides latest-tag auto-detection).",
    )
    changelog_draft_parser.add_argument(
        "--output",
        default=None,
        help="Write rendered output to a file path instead of stdout.",
    )
    changelog_draft_parser.set_defaults(func=cmd_changelog_draft)

    changelog_payload_parser = changelog_subparsers.add_parser(
        "payload",
        help="Render deterministic model-ready payload JSON from release context.",
    )
    changelog_payload_parser.add_argument(
        "--since-tag",
        default=None,
        help="Optional tag to use as lower bound (overrides latest-tag auto-detection).",
    )
    changelog_payload_parser.add_argument(
        "--output",
        default=None,
        help="Write rendered output to a file path instead of stdout.",
    )
    changelog_payload_parser.set_defaults(func=cmd_changelog_payload)

    changelog_ai_draft_parser = changelog_subparsers.add_parser(
        "ai-draft",
        help="Generate an AI-assisted changelog draft from deterministic payload input.",
    )
    changelog_ai_draft_parser.add_argument(
        "--since-tag",
        default=None,
        help="Optional tag to use as lower bound (overrides latest-tag auto-detection).",
    )
    changelog_ai_draft_parser.add_argument(
        "--output",
        default=None,
        help="Write rendered markdown draft to a file path instead of stdout.",
    )
    changelog_ai_draft_parser.add_argument(
        "--save-json",
        default=None,
        help="Optional path to save the validated structured AI draft JSON.",
    )
    changelog_ai_draft_parser.add_argument(
        "--model",
        default=DEFAULT_OPENAI_MINI_MODEL,
        help=f"OpenAI model to use (default: {DEFAULT_OPENAI_MINI_MODEL}).",
    )
    changelog_ai_draft_parser.set_defaults(func=cmd_changelog_ai_draft)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    try:
        return int(args.func(args))
    except KeyboardInterrupt:
        print("Interrupted.", file=sys.stderr)
        return 130
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


def cmd_check(args: argparse.Namespace) -> int:
    state = get_release_state(args.repo_root)
    print_release_state(state)

    if state.has_structural_errors:
        return 1
    if state.has_drift:
        print()
        print(build_sync_required_message(state))
        return 1
    return 0


def cmd_sync(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    state = require_release_files(repo_root)
    paths = state.paths

    canonical = state.versions.canonical
    if canonical is None:
        raise ValueError("Canonical VERSION could not be resolved")

    debian_revision = resolve_debian_revision(
        explicit_revision=args.debian_revision,
        current_debian=state.versions.debian,
    )

    print(f"Canonical version: {canonical}")
    print(f"Debian revision:   {debian_revision}")
    print()

    print_planned_changes(
        current_canonical=state.versions.canonical,
        current_meson=state.versions.meson,
        current_package_json=state.versions.package_json,
        current_debian=state.versions.debian,
        target_version=canonical,
        target_debian_revision=debian_revision,
    )

    if args.dry_run:
        print("\nDry run only. No files were modified.")
        return 0

    apply_version_update(
        paths=paths,
        version=canonical,
        debian_revision=debian_revision,
        write_canonical=False,
    )

    print("\nSync complete.")
    return 0


def cmd_set_version(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    _ = require_synced_release_state(repo_root)

    target_version = Version.parse(args.version)
    debian_revision = args.debian_revision
    state = get_release_state(repo_root)
    paths = state.paths

    print(f"Target version:    {target_version}")
    print(f"Debian revision:   {debian_revision}")
    print()

    print_planned_changes(
        current_canonical=state.versions.canonical,
        current_meson=state.versions.meson,
        current_package_json=state.versions.package_json,
        current_debian=state.versions.debian,
        target_version=target_version,
        target_debian_revision=debian_revision,
    )

    if args.dry_run:
        print("\nDry run only. No files were modified.")
        return 0

    apply_version_update(
        paths=paths,
        version=target_version,
        debian_revision=debian_revision,
        write_canonical=True,
    )

    print("\nVersion update complete.")
    return 0


def cmd_bump(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    state = require_synced_release_state(repo_root)

    current_version = state.versions.canonical
    if current_version is None:
        raise ValueError("Could not determine canonical version from VERSION")

    target_version = bump_version(current_version, args.part)

    print(f"Current version:   {current_version}")
    print(f"Bump part:         {args.part}")
    print(f"Target version:    {target_version}")
    print()

    set_args = argparse.Namespace(
        repo_root=str(repo_root),
        version=str(target_version),
        debian_revision=args.debian_revision,
        dry_run=args.dry_run,
    )
    return cmd_set_version(set_args)


def cmd_changelog_draft(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    context = build_changelog_context(repo_root, args.since_tag)

    if args.format == "json":
        rendered = f"{render_debug_json(context)}\n"
    else:
        rendered = render_release_changelog(context)

    write_output(rendered, args.output)
    if args.output:
        print(f"Wrote changelog draft to {Path(args.output).resolve()}")
    return 0


def cmd_changelog_payload(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    context = build_changelog_context(repo_root, args.since_tag)
    payload = build_ai_payload(context)
    rendered = render_ai_payload_json(payload)

    write_output(rendered, args.output)
    if args.output:
        print(f"Wrote changelog payload to {Path(args.output).resolve()}")
    return 0


def cmd_changelog_ai_draft(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    context = build_changelog_context(repo_root, args.since_tag)
    payload = build_ai_payload(context)

    draft = generate_mini_draft_from_payload(payload, model=args.model)
    markdown = render_ai_draft_markdown(draft)
    write_output(markdown, args.output)

    if args.output:
        print(f"Wrote AI changelog draft to {Path(args.output).resolve()}")

    if args.save_json:
        write_output(render_ai_draft_json(draft), args.save_json)
        print(f"Wrote AI draft JSON to {Path(args.save_json).resolve()}")

    return 0


def bump_version(version: Version, part: str) -> Version:
    if part == "major":
        return version.bump_major()
    if part == "minor":
        return version.bump_minor()
    if part == "patch":
        return version.bump_patch()
    raise ValueError(f"Unsupported bump part: {part}")


def print_release_state(state) -> None:
    print("Release state")
    print("-------------")
    print(f"Repo root:         {state.paths.repo_root}")
    print(f"VERSION:           {format_value(state.versions.canonical)}")
    print(f"meson.build:       {format_value(state.versions.meson)}")
    print(f"package.json:      {format_value(state.versions.package_json)}")
    print(f"debian/changelog:  {format_value(state.versions.debian)}")
    print(f"Status:            {'OK' if state.is_valid else 'INVALID'}")

    if state.issues:
        print("\nIssues:")
        for issue in state.issues:
            print(f"  - [{issue.code}] {issue.message}")


def print_planned_changes(
    *,
    current_canonical,
    current_meson,
    current_package_json,
    current_debian,
    target_version: Version,
    target_debian_revision: int,
) -> None:
    print("Planned changes")
    print("---------------")
    print(f"VERSION:           {format_value(current_canonical)} -> {target_version}")
    print(f"meson.build:       {format_value(current_meson)} -> {target_version}")
    print(f"package.json:      {format_value(current_package_json)} -> {target_version}")
    print(
        f"debian/changelog:  {format_value(current_debian)} -> "
        f"{target_version}-{target_debian_revision}"
    )


def format_value(value) -> str:
    return str(value) if value is not None else "<unavailable>"


def write_output(content: str, output_path: str | None) -> None:
    if output_path is None:
        print(content, end="" if content.endswith("\n") else "\n")
        return

    target = Path(output_path)
    try:
        target.write_text(content, encoding="utf-8")
    except Exception as exc:
        raise ValueError(f"Failed to write output to {target}: {exc}") from exc


def build_changelog_context(repo_root: Path, since_tag: str | None):
    version_path = repo_root / "VERSION"

    try:
        version = read_version_file(version_path)
    except Exception as exc:
        raise ValueError(f"Failed to read canonical version from {version_path}: {exc}") from exc

    return build_release_context(
        version=str(version),
        repo_root=repo_root,
        previous_tag=since_tag,
    )
