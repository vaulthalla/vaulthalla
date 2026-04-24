from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import os
import re
import shutil
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

from tools.release.changelog.ai.config import (
    AIPipelineCLIOverrides,
    AIPipelineConfig,
    AIProviderConfig,
    AIStageName,
    DEFAULT_AI_DRAFT_MODEL,
    DEFAULT_AI_PROVIDER_KIND,
    resolve_ai_pipeline_config,
)
from tools.release.changelog.ai.contracts.polish import AIPolishResult
from tools.release.changelog.ai.contracts.triage import build_triage_ir_payload
from tools.release.changelog.ai.failure_artifacts import (
    collect_provider_failure_evidence,
    provider_response_observed,
    write_failure_artifact,
)
from tools.release.changelog.ai.providers import (
    ProviderPreflightResult,
    build_structured_json_provider,
    run_provider_preflight,
)
from tools.release.changelog.ai.providers.base import StructuredJSONProvider
from tools.release.changelog.ai.render.markdown import render_draft_markdown, render_polish_markdown
from tools.release.changelog.ai.stages.draft import generate_draft_from_payload, render_draft_result_json
from tools.release.changelog.ai.stages.emergency_triage import (
    build_triage_input_from_emergency_result,
    render_emergency_triage_result_json,
    run_emergency_triage_stage,
)
from tools.release.changelog.ai.stages.polish import render_polish_result_json, run_polish_stage
from tools.release.changelog.ai.stages.release_notes import run_release_notes_stage
from tools.release.changelog.ai.stages.triage import render_triage_result_json, run_triage_stage
from tools.release.changelog.release_workflow import (
    DEFAULT_CACHED_DRAFT_PATH,
    DEFAULT_CHANGELOG_SCRATCH_DIR,
    parse_release_ai_settings,
    render_cached_draft_markdown,
    refresh_debian_changelog_entry,
    resolve_release_changelog,
)
from tools.release.changelog.context_builder import build_release_context
from tools.release.changelog.payload import (
    build_ai_payload,
    build_semantic_ai_payload,
    render_ai_payload_json,
    render_semantic_ai_payload_json,
)
from tools.release.changelog.render_raw import render_debug_json, render_release_changelog
from tools.release.packaging import (
    build_debian_package,
    publish_debian_artifacts,
    resolve_debian_publication_settings,
    validate_release_artifacts,
)
from tools.release.version.adapters.version_file import read_version_file
from tools.release.version.models import Version
from tools.release.version.sync import apply_version_update, resolve_debian_revision
from tools.release.version.validate import (
    build_sync_required_message,
    get_release_state,
    require_release_files,
    require_synced_release_state,
)

DEFAULT_CHANGELOG_DRAFT_OUTPUT = str(DEFAULT_CACHED_DRAFT_PATH)
DEFAULT_CHANGELOG_RELEASE_OUTPUT = str(DEFAULT_CHANGELOG_SCRATCH_DIR / "changelog.release.md")
DEFAULT_CHANGELOG_RAW_OUTPUT = str(DEFAULT_CHANGELOG_SCRATCH_DIR / "changelog.raw.md")
DEFAULT_CHANGELOG_PAYLOAD_OUTPUT = str(DEFAULT_CHANGELOG_SCRATCH_DIR / "changelog.payload.json")
DEFAULT_CHANGELOG_SEMANTIC_PAYLOAD_OUTPUT = str(DEFAULT_CHANGELOG_SCRATCH_DIR / "changelog.semantic_payload.json")
DEFAULT_RELEASE_NOTES_OUTPUT = str(DEFAULT_CHANGELOG_SCRATCH_DIR / "release_notes.md")
DEFAULT_CHANGELOG_COMPARISON_DIR = DEFAULT_CHANGELOG_SCRATCH_DIR / "comparisons"


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

    build_deb_parser = subparsers.add_parser(
        "build-deb",
        help="Build Debian package artifacts into a local release directory.",
    )
    build_deb_parser.add_argument(
        "--output-dir",
        default="release",
        help="Output directory for copied Debian artifacts (default: release/ at repo root).",
    )
    build_deb_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Validate and print planned build actions without running dpkg-buildpackage.",
    )
    build_deb_parser.set_defaults(func=cmd_build_deb)

    validate_release_artifacts_parser = subparsers.add_parser(
        "validate-release-artifacts",
        help="Validate staged release artifacts before upload/publication.",
    )
    validate_release_artifacts_parser.add_argument(
        "--output-dir",
        default="release",
        help="Release artifact directory to validate (default: release/ at repo root).",
    )
    validate_release_artifacts_parser.add_argument(
        "--skip-changelog",
        action="store_true",
        help="Skip changelog artifact checks.",
    )
    validate_release_artifacts_parser.set_defaults(func=cmd_validate_release_artifacts)

    publish_deb_parser = subparsers.add_parser(
        "publish-deb",
        help="Publish staged Debian package artifacts to Nexus.",
    )
    publish_deb_parser.add_argument(
        "--output-dir",
        default="release",
        help="Release artifact directory that contains Debian packages to publish (default: release/ at repo root).",
    )
    publish_deb_parser.add_argument(
        "--mode",
        default=None,
        help="Publication mode override (disabled|nexus). Defaults to RELEASE_PUBLISH_MODE env.",
    )
    publish_deb_parser.add_argument(
        "--nexus-repo-url",
        default=None,
        help="Nexus repository URL override. Defaults to NEXUS_REPO_URL env.",
    )
    publish_deb_parser.add_argument(
        "--nexus-user",
        default=None,
        help="Nexus username override. Defaults to NEXUS_USER env.",
    )
    publish_deb_parser.add_argument(
        "--nexus-pass",
        default=None,
        help="Nexus password override. Defaults to NEXUS_PASS env.",
    )
    publish_deb_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Validate publication settings and artifact selection without uploading.",
    )
    publish_deb_parser.add_argument(
        "--require-enabled",
        action="store_true",
        help="Fail if publication mode resolves to disabled for this run.",
    )
    publish_deb_parser.set_defaults(func=cmd_publish_deb)

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

    changelog_release_parser = changelog_subparsers.add_parser(
        "release",
        help="Generate release changelog artifacts with deterministic AI/manual fallback behavior.",
    )
    changelog_release_parser.add_argument(
        "--since-tag",
        default=None,
        help="Optional tag to use as lower bound (overrides latest-tag auto-detection).",
    )
    changelog_release_parser.add_argument(
        "--output",
        default=DEFAULT_CHANGELOG_RELEASE_OUTPUT,
        help="Output markdown path for the selected release changelog path.",
    )
    changelog_release_parser.add_argument(
        "--raw-output",
        default=DEFAULT_CHANGELOG_RAW_OUTPUT,
        help="Output path for deterministic raw changelog evidence.",
    )
    changelog_release_parser.add_argument(
        "--payload-output",
        default=DEFAULT_CHANGELOG_PAYLOAD_OUTPUT,
        help="Output path for deterministic AI payload evidence JSON.",
    )
    changelog_release_parser.add_argument(
        "--semantic-payload-output",
        default=DEFAULT_CHANGELOG_SEMANTIC_PAYLOAD_OUTPUT,
        help="Output path for deterministic semantic payload evidence JSON.",
    )
    changelog_release_parser.add_argument(
        "--release-notes-output",
        default=DEFAULT_RELEASE_NOTES_OUTPUT,
        help="Output path for optional release_notes markdown when AI path and profile stage enable it.",
    )
    changelog_release_parser.add_argument(
        "--manual-changelog-path",
        default="debian/changelog",
        help="Debian changelog file used for manual fallback and release entry updates.",
    )
    changelog_release_parser.add_argument(
        "--cached-draft-path",
        default=DEFAULT_CHANGELOG_DRAFT_OUTPUT,
        help="Cached draft markdown source path used when live AI providers are unavailable.",
    )
    changelog_release_parser.add_argument(
        "--debian-distribution",
        default=None,
        help=(
            "Override Debian changelog distribution token (e.g. unstable/stable). "
            "Defaults to RELEASE_DEBIAN_DISTRIBUTION env or current top-entry distribution."
        ),
    )
    changelog_release_parser.add_argument(
        "--debian-urgency",
        default=None,
        help=(
            "Override Debian changelog urgency token (e.g. low/medium/high). "
            "Defaults to RELEASE_DEBIAN_URGENCY env or current top-entry urgency."
        ),
    )
    changelog_release_parser.set_defaults(func=cmd_changelog_release)

    changelog_ai_check_parser = changelog_subparsers.add_parser(
        "ai-check",
        help="Validate AI provider connectivity and model availability.",
    )
    changelog_ai_check_parser.add_argument(
        "--ai-profile",
        default=None,
        help="Named AI profile slug from ai.yml at repo root.",
    )
    changelog_ai_check_parser.add_argument(
        "--model",
        default=None,
        help=(
            "Override model for this check (defaults resolved from built-ins/profile; "
            f"legacy default: {DEFAULT_AI_DRAFT_MODEL})."
        ),
    )
    changelog_ai_check_parser.add_argument(
        "--provider",
        choices=("openai", "openai-compatible"),
        default=None,
        help=f"AI provider transport override (default resolved from config; legacy: {DEFAULT_AI_PROVIDER_KIND}).",
    )
    changelog_ai_check_parser.add_argument(
        "--base-url",
        default=None,
        help="OpenAI-compatible endpoint base URL (for --provider openai-compatible).",
    )
    changelog_ai_check_parser.set_defaults(func=cmd_changelog_ai_check)

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
        help=(
            "Write rendered markdown draft to a file path. "
            "When omitted, draft still persists to .changelog_scratch/changelog.draft.md."
        ),
    )
    changelog_ai_draft_parser.add_argument(
        "--save-json",
        default=None,
        help="Optional path to save the validated structured AI draft JSON.",
    )
    changelog_ai_draft_parser.add_argument(
        "--ai-profile",
        default=None,
        help="Named AI profile slug from ai.yml at repo root.",
    )
    changelog_ai_draft_parser.add_argument(
        "--model",
        default=None,
        help=(
            "Global model override for all AI stages (emergency_triage/triage/draft/polish/release_notes). "
            f"Legacy default: {DEFAULT_AI_DRAFT_MODEL}."
        ),
    )
    changelog_ai_draft_parser.add_argument(
        "--provider",
        choices=("openai", "openai-compatible"),
        default=None,
        help=f"AI provider transport override (default resolved from config; legacy: {DEFAULT_AI_PROVIDER_KIND}).",
    )
    changelog_ai_draft_parser.add_argument(
        "--base-url",
        default=None,
        help="OpenAI-compatible endpoint base URL (for --provider openai-compatible).",
    )
    changelog_ai_draft_parser.add_argument(
        "--use-triage",
        action="store_true",
        help="Run optional AI triage stage before draft generation.",
    )
    changelog_ai_draft_parser.add_argument(
        "--save-triage-json",
        default=None,
        help="Optional path to save validated structured triage JSON when --use-triage is enabled.",
    )
    changelog_ai_draft_parser.add_argument(
        "--polish",
        action="store_true",
        help="Run optional editorial polish stage after draft generation.",
    )
    changelog_ai_draft_parser.add_argument(
        "--save-polish-json",
        default=None,
        help="Optional path to save validated structured polish JSON when --polish is enabled.",
    )
    changelog_ai_draft_parser.add_argument(
        "--release-notes-output",
        default=DEFAULT_RELEASE_NOTES_OUTPUT,
        help="Output path for optional release_notes markdown when stage is enabled by profile.",
    )
    changelog_ai_draft_parser.set_defaults(func=cmd_changelog_ai_draft)

    changelog_ai_release_parser = changelog_subparsers.add_parser(
        "ai-release",
        help="Generate AI changelog draft and deterministically finalize Debian changelog in one flow.",
    )
    changelog_ai_release_parser.add_argument(
        "--since-tag",
        default=None,
        help="Optional tag to use as lower bound (overrides latest-tag auto-detection).",
    )
    changelog_ai_release_parser.add_argument(
        "--draft-output",
        default=DEFAULT_CHANGELOG_DRAFT_OUTPUT,
        help="Path where AI draft markdown is persisted before release finalization.",
    )
    changelog_ai_release_parser.add_argument(
        "--output",
        default=DEFAULT_CHANGELOG_RELEASE_OUTPUT,
        help="Output markdown path for the selected release changelog path.",
    )
    changelog_ai_release_parser.add_argument(
        "--raw-output",
        default=DEFAULT_CHANGELOG_RAW_OUTPUT,
        help="Output path for deterministic raw changelog evidence.",
    )
    changelog_ai_release_parser.add_argument(
        "--payload-output",
        default=DEFAULT_CHANGELOG_PAYLOAD_OUTPUT,
        help="Output path for deterministic AI payload evidence JSON.",
    )
    changelog_ai_release_parser.add_argument(
        "--semantic-payload-output",
        default=DEFAULT_CHANGELOG_SEMANTIC_PAYLOAD_OUTPUT,
        help="Output path for deterministic semantic payload evidence JSON.",
    )
    changelog_ai_release_parser.add_argument(
        "--manual-changelog-path",
        default="debian/changelog",
        help="Debian changelog file used for manual fallback and release entry updates.",
    )
    changelog_ai_release_parser.add_argument(
        "--debian-distribution",
        default=None,
        help="Override Debian changelog distribution token (e.g. unstable/stable).",
    )
    changelog_ai_release_parser.add_argument(
        "--debian-urgency",
        default=None,
        help="Override Debian changelog urgency token (e.g. low/medium/high).",
    )
    changelog_ai_release_parser.add_argument(
        "--save-json",
        default=None,
        help="Optional path to save validated structured AI draft JSON.",
    )
    changelog_ai_release_parser.add_argument(
        "--ai-profile",
        default=None,
        help="Named AI profile slug from ai.yml at repo root.",
    )
    changelog_ai_release_parser.add_argument(
        "--model",
        default=None,
        help=(
            "Global model override for all AI stages (emergency_triage/triage/draft/polish/release_notes). "
            f"Legacy default: {DEFAULT_AI_DRAFT_MODEL}."
        ),
    )
    changelog_ai_release_parser.add_argument(
        "--provider",
        choices=("openai", "openai-compatible"),
        default=None,
        help=f"AI provider transport override (default resolved from config; legacy: {DEFAULT_AI_PROVIDER_KIND}).",
    )
    changelog_ai_release_parser.add_argument(
        "--base-url",
        default=None,
        help="OpenAI-compatible endpoint base URL (for --provider openai-compatible).",
    )
    changelog_ai_release_parser.add_argument(
        "--use-triage",
        action="store_true",
        help="Run optional AI triage stage before draft generation.",
    )
    changelog_ai_release_parser.add_argument(
        "--save-triage-json",
        default=None,
        help="Optional path to save validated structured triage JSON when --use-triage is enabled.",
    )
    changelog_ai_release_parser.add_argument(
        "--polish",
        action="store_true",
        help="Run optional editorial polish stage after draft generation.",
    )
    changelog_ai_release_parser.add_argument(
        "--save-polish-json",
        default=None,
        help="Optional path to save validated structured polish JSON when --polish is enabled.",
    )
    changelog_ai_release_parser.add_argument(
        "--release-notes-output",
        default=DEFAULT_RELEASE_NOTES_OUTPUT,
        help="Output path for optional release_notes markdown when stage is enabled by profile.",
    )
    changelog_ai_release_parser.set_defaults(func=cmd_changelog_ai_release)

    changelog_ai_compare_parser = changelog_subparsers.add_parser(
        "ai-compare",
        help="Generate one comparison artifact by running AI changelog generation across profiles.",
    )
    changelog_ai_compare_parser.add_argument(
        "--since-tag",
        default=None,
        help="Optional tag to use as lower bound (overrides latest-tag auto-detection).",
    )
    changelog_ai_compare_parser.add_argument(
        "--ai-profiles",
        default=None,
        help="Comma-separated AI profile slugs to compare (e.g. openai-cheap,openai-balanced).",
    )
    changelog_ai_compare_parser.add_argument(
        "--output-name",
        default=None,
        help="Output file name under .changelog_scratch/comparisons/.",
    )
    changelog_ai_compare_parser.set_defaults(func=cmd_changelog_ai_compare)

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

    should_clear_scratch = (
        state.versions.debian is not None and state.versions.debian.upstream != canonical
    )
    apply_version_update(
        paths=paths,
        version=canonical,
        debian_revision=debian_revision,
        write_canonical=False,
    )
    if should_clear_scratch:
        clear_changelog_scratch(repo_root, reason=f"VERSION transition to {canonical} via sync")
    # Enforce that sync leaves the repo in a fully consistent release state.
    _ = require_synced_release_state(repo_root)

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

    previous_canonical = state.versions.canonical
    apply_version_update(
        paths=paths,
        version=target_version,
        debian_revision=debian_revision,
        write_canonical=True,
    )
    if previous_canonical is not None and previous_canonical != target_version:
        clear_changelog_scratch(repo_root, reason=f"VERSION bump {previous_canonical} -> {target_version}")

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


def cmd_build_deb(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    result = build_debian_package(
        repo_root=repo_root,
        output_dir=args.output_dir,
        dry_run=args.dry_run,
    )

    print("Debian build plan")
    print("----------------")
    print(f"Repo root:   {result.repo_root}")
    print(f"Package:     {result.package_name}")
    print(f"Version:     {result.package_version}")
    print(f"Output dir:  {result.output_dir}")
    print(f"Command:     {' '.join(result.command)}")

    if result.dry_run:
        print("\nDry run only. No Debian build command was executed.")
        return 0

    print()
    print("Artifacts")
    print("---------")
    for artifact in result.artifacts:
        print(f"- {artifact}")
    if result.build_log is not None:
        print(f"\nBuild log: {result.build_log}")

    return 0


def cmd_validate_release_artifacts(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = (repo_root / output_dir).resolve()

    result = validate_release_artifacts(
        output_dir=output_dir,
        require_changelog=not bool(args.skip_changelog),
    )
    print("Release artifact validation")
    print("---------------------------")
    print(f"Output dir:        {result.output_dir}")
    print(f"Debian artifacts:  {len(result.debian_artifacts)}")
    print(f"Web artifacts:     {len(result.web_artifacts)}")
    if not args.skip_changelog:
        print(f"Changelog files:   {len(result.changelog_artifacts)}")
    print("Status:            OK")
    return 0


def cmd_publish_deb(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = (repo_root / output_dir).resolve()

    settings = resolve_debian_publication_settings(
        mode=args.mode,
        nexus_repo_url=args.nexus_repo_url,
        nexus_user=args.nexus_user,
        nexus_password=args.nexus_pass,
        env=os.environ,
    )
    result = publish_debian_artifacts(
        output_dir=output_dir,
        settings=settings,
        dry_run=bool(args.dry_run),
        require_enabled=bool(args.require_enabled),
    )

    print("Debian publication")
    print("------------------")
    print(f"Publication required: {'yes' if args.require_enabled else 'no'}")
    print(f"Mode:              {result.mode}")
    print("Upload mode:       post-binary-to-base-url")
    print("Append filename:   no")
    print(f"Output dir:        {result.output_dir}")
    print(f"Debian artifacts:  {len(result.artifacts)}")
    for artifact in result.artifacts:
        print(f"- {artifact}")

    if not result.enabled:
        print("Status:            SKIPPED")
        print(f"Reason:            {result.skipped_reason or 'Publication disabled.'}")
        return 0

    print(f"Target URLs:       {len(result.target_urls)}")
    for target_url in result.target_urls:
        print(f"- {target_url}")

    if result.dry_run:
        print("Status:            DRY-RUN (validated, not uploaded)")
    else:
        print("Status:            PUBLISHED")
    return 0


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


def cmd_changelog_release(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    env_settings = parse_release_ai_settings(os.environ)
    _log_release_ai_preflight(env_settings)
    return _run_changelog_release_with_settings(
        args,
        repo_root=repo_root,
        env_settings=env_settings,
    )


def cmd_changelog_ai_release(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    draft_output_path = Path(args.draft_output)
    if not draft_output_path.is_absolute():
        draft_output_path = (repo_root / draft_output_path).resolve()
    draft_output = str(draft_output_path)

    _print_status("Changelog ai-release stage: generate AI draft artifact")
    draft_args = argparse.Namespace(
        repo_root=args.repo_root,
        changelog_command="ai-release",
        since_tag=args.since_tag,
        output=draft_output,
        save_json=args.save_json,
        model=args.model,
        provider=args.provider,
        base_url=args.base_url,
        ai_profile=args.ai_profile,
        use_triage=args.use_triage,
        save_triage_json=args.save_triage_json,
        polish=args.polish,
        save_polish_json=args.save_polish_json,
        release_notes_output=args.release_notes_output,
    )
    draft_rc = cmd_changelog_ai_draft(draft_args)
    if draft_rc != 0:
        return int(draft_rc)

    _print_status("Changelog ai-release stage: finalize release using freshly cached draft source")
    release_args = argparse.Namespace(
        repo_root=args.repo_root,
        since_tag=args.since_tag,
        output=args.output,
        raw_output=args.raw_output,
        payload_output=args.payload_output,
        semantic_payload_output=args.semantic_payload_output,
        release_notes_output=args.release_notes_output,
        manual_changelog_path=args.manual_changelog_path,
        cached_draft_path=draft_output,
        debian_distribution=args.debian_distribution,
        debian_urgency=args.debian_urgency,
    )

    previous_mode = os.environ.get("RELEASE_AI_MODE")
    os.environ["RELEASE_AI_MODE"] = "disabled"
    try:
        return cmd_changelog_release(release_args)
    finally:
        if previous_mode is None:
            os.environ.pop("RELEASE_AI_MODE", None)
        else:
            os.environ["RELEASE_AI_MODE"] = previous_mode


def cmd_changelog_ai_compare(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    profiles = _parse_ai_compare_profiles(args.ai_profiles)
    version = str(read_version_file(repo_root / "VERSION"))
    output_name = _resolve_ai_compare_output_name(args.output_name, version=version)

    comparisons_dir = (repo_root / DEFAULT_CHANGELOG_COMPARISON_DIR).resolve()
    comparisons_dir.mkdir(parents=True, exist_ok=True)
    output_path = (comparisons_dir / output_name).resolve()

    manual_changelog_path = (repo_root / "debian" / "changelog").resolve()
    if not manual_changelog_path.is_file():
        raise ValueError(f"Debian changelog file not found: {manual_changelog_path}")
    original_debian_changelog = manual_changelog_path.read_text(encoding="utf-8")

    sections: list[str] = [f"# AI Changelog Comparison — {version}"]
    with TemporaryDirectory(prefix="ai-compare-") as temp_dir:
        temp_root = Path(temp_dir)
        for profile in profiles:
            compare_args = argparse.Namespace(
                repo_root=str(repo_root),
                changelog_command="ai-compare",
                since_tag=args.since_tag,
                output=str(temp_root / f"{profile}.draft.md"),
                save_json=None,
                ai_profile=profile,
                model=None,
                provider=None,
                base_url=None,
                use_triage=False,
                save_triage_json=None,
                polish=False,
                save_polish_json=None,
                release_notes_output=str(temp_root / f"{profile}.release_notes.md"),
            )
            pipeline_config = build_ai_pipeline_config_from_args(compare_args, repo_root=repo_root)
            run_rc = cmd_changelog_ai_draft(compare_args)
            if run_rc != 0:
                raise ValueError(f"AI compare run failed for profile `{profile}` (exit code {run_rc}).")

            cached_draft = Path(compare_args.output).read_text(encoding="utf-8")
            release_markdown = _strip_cached_draft_marker(cached_draft)
            if not release_markdown.strip():
                raise ValueError(f"AI compare produced empty markdown for profile `{profile}`.")

            profile_debian = temp_root / f"{profile}.debian.changelog"
            profile_debian.write_text(original_debian_changelog, encoding="utf-8")
            _ = refresh_debian_changelog_entry(
                changelog_path=profile_debian,
                release_markdown=release_markdown,
                environ=os.environ,
            )
            debian_output = profile_debian.read_text(encoding="utf-8")

            sections.extend(
                [
                    "",
                    f"## Profile: {profile}",
                    "",
                    "### Effective profile config",
                    "```yaml",
                    _render_ai_compare_profile_config_yaml(pipeline_config).rstrip(),
                    "```",
                    "",
                    "### changelog.release.md",
                    "```markdown",
                    release_markdown.rstrip(),
                    "```",
                    "",
                    "### release_notes.md",
                    "```markdown",
                    _read_release_notes_for_compare(compare_args.release_notes_output),
                    "```",
                    "",
                    "### debian/changelog",
                    "```text",
                    debian_output.rstrip(),
                    "```",
                    "",
                    "---",
                ]
            )

    rendered = "\n".join(sections).rstrip() + "\n"
    write_output(rendered, str(output_path))
    print(f"Wrote AI comparison artifact to {output_path}")
    return 0


def cmd_changelog_ai_draft(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    context = build_changelog_context(repo_root, args.since_tag)
    payload = build_ai_payload(context)
    pipeline_config = build_ai_pipeline_config_from_args(args, repo_root=repo_root)
    run_triage = bool(args.use_triage) or pipeline_config.is_stage_enabled("triage")
    run_emergency_triage = pipeline_config.is_stage_enabled("emergency_triage")
    run_polish = bool(args.polish) or pipeline_config.is_stage_enabled("polish")
    run_release_notes = pipeline_config.is_stage_enabled("release_notes")
    if run_emergency_triage and not run_triage:
        raise ValueError("`emergency_triage` stage requires triage stage to run.")
    if args.save_triage_json and not run_triage:
        raise ValueError("--save-triage-json requires triage stage to run.")
    if args.save_polish_json and not run_polish:
        raise ValueError("--save-polish-json requires polish stage to run.")
    draft_provider = build_ai_provider_from_args(args, repo_root=repo_root, stage="draft")

    # Local-compatible runs should fail early with explicit endpoint/model diagnostics.
    if pipeline_config.provider == "openai-compatible":
        _run_stage_preflight(
            args=args,
            repo_root=repo_root,
            draft_provider=draft_provider,
            include_emergency_triage=run_emergency_triage and run_triage,
            include_triage=run_triage,
            include_polish=run_polish,
            include_release_notes=run_release_notes,
        )

    semantic_payload: dict | None = build_semantic_ai_payload(context) if run_triage else None

    draft_input: dict = payload
    source_kind = "payload"
    triage_input_mode = "raw_semantic"
    triage_input_payload: dict = semantic_payload if semantic_payload is not None else payload
    emergency_triage_stage_cfg = pipeline_config.stages["emergency_triage"]
    triage_stage_cfg = pipeline_config.stages["triage"]
    draft_stage_cfg = pipeline_config.stages["draft"]
    polish_stage_cfg = pipeline_config.stages["polish"]
    release_notes_stage_cfg = pipeline_config.stages["release_notes"]

    if run_triage:
        if run_emergency_triage:
            emergency_provider = build_ai_provider_from_args(
                args,
                repo_root=repo_root,
                stage="emergency_triage",
            )
            try:
                print("Emergency triage: batch aggregation start")
                emergency_result = run_emergency_triage_stage(
                    semantic_payload if semantic_payload is not None else payload,
                    provider=emergency_provider,
                    provider_kind=pipeline_config.provider,
                    reasoning_effort=emergency_triage_stage_cfg.reasoning_effort,
                    structured_mode=emergency_triage_stage_cfg.structured_mode,
                    temperature=emergency_triage_stage_cfg.temperature,
                    max_output_tokens_policy=emergency_triage_stage_cfg.max_output_tokens,
                    progress_logger=print,
                )
                print("Emergency triage: batch aggregation end")
            except Exception as exc:
                _capture_stage_failure_artifact(
                    repo_root=repo_root,
                    args=args,
                    stage="emergency_triage",
                    pipeline_config=pipeline_config,
                    provider=emergency_provider,
                    exc=exc,
                    stage_settings={
                        "structured_mode": emergency_triage_stage_cfg.structured_mode,
                        "reasoning_effort": emergency_triage_stage_cfg.reasoning_effort,
                        "temperature": emergency_triage_stage_cfg.temperature,
                        "max_output_tokens_policy": str(emergency_triage_stage_cfg.max_output_tokens),
                    },
                )
                raise _stage_failure("Emergency triage", exc) from exc

            emergency_output = str((repo_root / DEFAULT_CHANGELOG_SCRATCH_DIR / "emergency_triage.json").resolve())
            print("Emergency triage: artifact write start")
            write_output(render_emergency_triage_result_json(emergency_result), emergency_output)
            print("Emergency triage: artifact write end")
            if emergency_output != "-":
                print(f"Wrote emergency triage artifact to {Path(emergency_output).resolve()}")
            if semantic_payload is not None:
                triage_input_payload = build_triage_input_from_emergency_result(
                    semantic_payload,
                    emergency_result,
                )
                triage_input_mode = "synthesized_semantic"

        triage_provider = build_ai_provider_from_args(args, repo_root=repo_root, stage="triage")
        try:
            triage_result = run_triage_stage(
                triage_input_payload,
                provider=triage_provider,
                provider_kind=pipeline_config.provider,
                reasoning_effort=triage_stage_cfg.reasoning_effort,
                structured_mode=triage_stage_cfg.structured_mode,
                temperature=triage_stage_cfg.temperature,
                max_output_tokens_policy=triage_stage_cfg.max_output_tokens,
                input_mode=triage_input_mode,
            )
        except Exception as exc:
            _capture_stage_failure_artifact(
                repo_root=repo_root,
                args=args,
                stage="triage",
                pipeline_config=pipeline_config,
                provider=triage_provider,
                exc=exc,
                stage_settings={
                    "structured_mode": triage_stage_cfg.structured_mode,
                    "reasoning_effort": triage_stage_cfg.reasoning_effort,
                    "temperature": triage_stage_cfg.temperature,
                    "max_output_tokens_policy": str(triage_stage_cfg.max_output_tokens),
                },
            )
            raise _stage_failure("Triage", exc) from exc
        draft_input = build_triage_ir_payload(triage_result)
        source_kind = "triage"

        if args.save_triage_json:
            write_output(render_triage_result_json(triage_result), args.save_triage_json)
            print(f"Wrote AI triage JSON to {Path(args.save_triage_json).resolve()}")

    try:
        draft = generate_draft_from_payload(
            draft_input,
            provider=draft_provider,
            source_kind=source_kind,
            provider_kind=pipeline_config.provider,
            reasoning_effort=draft_stage_cfg.reasoning_effort,
            structured_mode=draft_stage_cfg.structured_mode,
            temperature=draft_stage_cfg.temperature,
            max_output_tokens_policy=draft_stage_cfg.max_output_tokens,
        )
    except Exception as exc:
        _capture_stage_failure_artifact(
            repo_root=repo_root,
            args=args,
            stage="draft",
            pipeline_config=pipeline_config,
            provider=draft_provider,
            exc=exc,
            stage_settings={
                "structured_mode": draft_stage_cfg.structured_mode,
                "reasoning_effort": draft_stage_cfg.reasoning_effort,
                "temperature": draft_stage_cfg.temperature,
                "max_output_tokens_policy": str(draft_stage_cfg.max_output_tokens),
                "source_kind": source_kind,
            },
        )
        raise _stage_failure("Draft", exc) from exc
    draft_markdown: str | None = None
    if run_release_notes or not run_polish:
        draft_markdown = render_draft_markdown(draft)
    polish_result: AIPolishResult | None = None
    release_notes_result = None
    polish_provider: StructuredJSONProvider | None = None
    release_notes_provider: StructuredJSONProvider | None = None

    if run_polish:
        polish_provider = build_ai_provider_from_args(args, repo_root=repo_root, stage="polish")
    if run_release_notes:
        release_notes_provider = build_ai_provider_from_args(args, repo_root=repo_root, stage="release_notes")

    def _run_polish() -> AIPolishResult:
        assert polish_provider is not None
        try:
            return run_polish_stage(
                draft,
                provider=polish_provider,
                provider_kind=pipeline_config.provider,
                reasoning_effort=polish_stage_cfg.reasoning_effort,
                structured_mode=polish_stage_cfg.structured_mode,
                temperature=polish_stage_cfg.temperature,
                max_output_tokens_policy=polish_stage_cfg.max_output_tokens,
            )
        except Exception as exc:
            _capture_stage_failure_artifact(
                repo_root=repo_root,
                args=args,
                stage="polish",
                pipeline_config=pipeline_config,
                provider=polish_provider,
                exc=exc,
                stage_settings={
                    "structured_mode": polish_stage_cfg.structured_mode,
                    "reasoning_effort": polish_stage_cfg.reasoning_effort,
                    "temperature": polish_stage_cfg.temperature,
                    "max_output_tokens_policy": str(polish_stage_cfg.max_output_tokens),
                },
            )
            raise _stage_failure("Polish", exc) from exc

    def _run_release_notes():
        assert release_notes_provider is not None
        assert draft_markdown is not None
        try:
            return run_release_notes_stage(
                draft_markdown,
                provider=release_notes_provider,
                provider_kind=pipeline_config.provider,
                reasoning_effort=release_notes_stage_cfg.reasoning_effort,
                structured_mode=release_notes_stage_cfg.structured_mode,
                temperature=release_notes_stage_cfg.temperature,
                max_output_tokens_policy=release_notes_stage_cfg.max_output_tokens,
            )
        except Exception as exc:
            _capture_stage_failure_artifact(
                repo_root=repo_root,
                args=args,
                stage="release_notes",
                pipeline_config=pipeline_config,
                provider=release_notes_provider,
                exc=exc,
                stage_settings={
                    "structured_mode": release_notes_stage_cfg.structured_mode,
                    "reasoning_effort": release_notes_stage_cfg.reasoning_effort,
                    "temperature": release_notes_stage_cfg.temperature,
                    "max_output_tokens_policy": str(release_notes_stage_cfg.max_output_tokens),
                },
            )
            raise _stage_failure("Release notes", exc) from exc

    if run_polish and run_release_notes:
        with ThreadPoolExecutor(max_workers=2, thread_name_prefix="vh-ai-final") as executor:
            futures = {
                "polish": executor.submit(_run_polish),
                "release_notes": executor.submit(_run_release_notes),
            }
            stage_errors: dict[str, Exception] = {}
            for stage_name in ("polish", "release_notes"):
                try:
                    result = futures[stage_name].result()
                except Exception as exc:  # pragma: no cover - exercised by stage failure tests.
                    stage_errors[stage_name] = exc if isinstance(exc, Exception) else RuntimeError(str(exc))
                    continue
                if stage_name == "polish":
                    polish_result = result
                else:
                    release_notes_result = result
            if stage_errors:
                first_failed_stage = "polish" if "polish" in stage_errors else "release_notes"
                raise stage_errors[first_failed_stage]
    elif run_polish:
        polish_result = _run_polish()
    elif run_release_notes:
        release_notes_result = _run_release_notes()

    if polish_result is not None and args.save_polish_json:
        write_output(render_polish_result_json(polish_result), args.save_polish_json)
        print(f"Wrote AI polish JSON to {Path(args.save_polish_json).resolve()}")

    if release_notes_result is not None:
        release_notes_output = getattr(args, "release_notes_output", DEFAULT_RELEASE_NOTES_OUTPUT)
        write_output(release_notes_result.markdown, release_notes_output)
        if release_notes_output != "-":
            print(f"Wrote AI release notes to {Path(release_notes_output).resolve()}")

    if polish_result is not None:
        final_markdown = render_polish_markdown(polish_result)
    else:
        assert draft_markdown is not None
        final_markdown = draft_markdown

    version = read_version_file(repo_root / "VERSION")
    cached_markdown = render_cached_draft_markdown(version=str(version), content=final_markdown)
    cached_target = args.output
    if cached_target is None or cached_target == "-":
        cached_target = DEFAULT_CHANGELOG_DRAFT_OUTPUT
    write_output(cached_markdown, cached_target)

    if args.output is None or args.output == "-":
        write_output(final_markdown, None)
    else:
        print(f"Wrote AI changelog draft to {Path(args.output).resolve()}")

    if args.save_json:
        if polish_result is not None:
            write_output(render_polish_result_json(polish_result), args.save_json)
        else:
            write_output(render_draft_result_json(draft), args.save_json)
        print(f"Wrote AI draft JSON to {Path(args.save_json).resolve()}")

    return 0


def cmd_changelog_ai_check(args: argparse.Namespace) -> int:
    provider_config = build_ai_provider_config_from_args(args)
    provider = build_ai_provider_from_config(provider_config)
    result = run_provider_preflight(provider_config, provider=provider, require_model=True)
    print_provider_preflight_result(result)
    return 0


def _run_changelog_release_with_settings(
    args: argparse.Namespace,
    *,
    repo_root: Path,
    env_settings,
) -> int:
    try:
        _print_status("Changelog release stage: build deterministic context + payload")
        context = build_changelog_context(repo_root, args.since_tag)
        payload = build_ai_payload(context)
        semantic_payload = build_semantic_ai_payload(context)
        raw_markdown = render_release_changelog(context)
        payload_json = render_ai_payload_json(payload)
        semantic_payload_json = render_semantic_ai_payload_json(semantic_payload)
    except Exception as exc:
        raise ValueError(f"Changelog release failed during context/payload generation: {exc}") from exc

    try:
        _print_status("Changelog release stage: write evidence artifacts")
        write_output(raw_markdown, args.raw_output)
        _print_status(f"Wrote changelog raw evidence to {Path(args.raw_output).resolve()}")
        write_output(payload_json, args.payload_output)
        _print_status(f"Wrote changelog payload evidence to {Path(args.payload_output).resolve()}")
        semantic_target = getattr(args, "semantic_payload_output", DEFAULT_CHANGELOG_SEMANTIC_PAYLOAD_OUTPUT)
        write_output(semantic_payload_json, semantic_target)
        _print_status(f"Wrote changelog semantic payload evidence to {Path(semantic_target).resolve()}")
    except Exception as exc:
        raise ValueError(f"Changelog release failed while writing evidence artifacts: {exc}") from exc

    try:
        _print_status("Changelog release stage: resolve changelog source")
        selection = resolve_release_changelog(
            repo_root=repo_root,
            payload=payload,
            semantic_payload=semantic_payload,
            settings=env_settings,
            manual_changelog_path=args.manual_changelog_path,
            cached_draft_path=getattr(args, "cached_draft_path", DEFAULT_CHANGELOG_DRAFT_OUTPUT),
            logger=_print_status,
        )
        selected_source = selection.path
        source_path = getattr(selection, "source_path", None)
        source_suffix = f" ({source_path})" if source_path is not None else ""
        _print_status(f"Selected changelog source: {selected_source}{source_suffix}")
    except Exception as exc:
        raise ValueError(f"Changelog release failed during source selection/generation: {exc}") from exc

    try:
        _print_status("Changelog release stage: write selected release changelog")
        write_output(selection.content, args.output)
        _print_status(f"Wrote release changelog to {Path(args.output).resolve()}")
        release_notes_output = getattr(args, "release_notes_output", DEFAULT_RELEASE_NOTES_OUTPUT)
        release_notes_content = getattr(selection, "release_notes_content", None)
        if release_notes_content is not None:
            write_output(release_notes_content, release_notes_output)
            _print_status(f"Wrote release notes artifact to {Path(release_notes_output).resolve()}")
        if selection.path == "local" and env_settings.local_base_url_override:
            if selection.local_base_url_overrode_profile:
                _print_status("Local base_url override status: applied from RELEASE_LOCAL_LLM_BASE_URL.")
            else:
                _print_status("Local base_url override status: set but not applied.")
    except Exception as exc:
        raise ValueError(f"Changelog release failed while writing selected output: {exc}") from exc

    if selection.path == "manual":
        _print_status(
            "Changelog release stage: Debian changelog entry update skipped "
            "(manual/no-AI fallback source selected)."
        )
        return 0

    try:
        _print_status("Changelog release stage: refresh Debian changelog top entry metadata + summary")
        updated = refresh_debian_changelog_entry(
            changelog_path=Path(args.manual_changelog_path)
            if Path(args.manual_changelog_path).is_absolute()
            else (repo_root / args.manual_changelog_path),
            release_markdown=selection.content,
            distribution=args.debian_distribution,
            urgency=args.debian_urgency,
            environ=os.environ,
        )
        _print_status(f"Updated Debian changelog entry at {updated.path}")
        _print_status(
            "Debian entry metadata: "
            f"{updated.package} ({updated.full_version}) {updated.distribution}; urgency={updated.urgency}"
        )
        _print_status(f"Debian entry maintainer: {updated.maintainer}")
        _print_status(f"Debian entry timestamp:  {updated.timestamp}")
    except Exception as exc:
        raise ValueError(f"Changelog release failed while updating Debian changelog: {exc}") from exc

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


def clear_changelog_scratch(repo_root: Path, *, reason: str) -> None:
    scratch_dir = (repo_root / DEFAULT_CHANGELOG_SCRATCH_DIR).resolve()
    if not scratch_dir.exists():
        return
    if not scratch_dir.is_dir():
        raise ValueError(f"Expected changelog scratch path to be a directory: {scratch_dir}")
    shutil.rmtree(scratch_dir)
    print(f"Cleared changelog scratch artifacts at {scratch_dir} ({reason}).")


def write_output(content: str, output_path: str | None) -> None:
    if output_path is None or output_path == "-":
        line = content if content.endswith("\n") else f"{content}\n"
        sys.stdout.write(line)
        sys.stdout.flush()
        return

    target = Path(output_path)
    try:
        parent = target.parent
        if parent != Path(""):
            parent.mkdir(parents=True, exist_ok=True)
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


def build_ai_pipeline_config_from_args(
    args: argparse.Namespace,
    *,
    repo_root: Path | None = None,
) -> AIPipelineConfig:
    active_repo_root = repo_root if repo_root is not None else Path(args.repo_root).resolve()
    overrides = AIPipelineCLIOverrides(
        provider=getattr(args, "provider", None),
        base_url=getattr(args, "base_url", None),
        model=getattr(args, "model", None),
    )
    return resolve_ai_pipeline_config(
        repo_root=active_repo_root,
        profile_slug=getattr(args, "ai_profile", None),
        cli_overrides=overrides,
    )


def _parse_ai_compare_profiles(raw: str | None) -> list[str]:
    if raw is None:
        raise ValueError("--ai-profiles is required.")
    parsed = [value.strip() for value in raw.split(",") if value.strip()]
    if not parsed:
        raise ValueError("--ai-profiles must include at least one profile slug.")
    ordered_unique: list[str] = []
    seen: set[str] = set()
    for profile in parsed:
        if profile in seen:
            continue
        seen.add(profile)
        ordered_unique.append(profile)
    return ordered_unique


def _resolve_ai_compare_output_name(raw: str | None, *, version: str) -> str:
    candidate = raw.strip() if isinstance(raw, str) else ""
    if not candidate:
        return f"ai-comparison-{version}.md"
    if candidate in {".", ".."}:
        raise ValueError("--output-name must be a file name, not a directory marker.")
    if "/" in candidate or "\\" in candidate:
        raise ValueError("--output-name must be a file name without path separators.")
    if Path(candidate).name != candidate:
        raise ValueError("--output-name must be a plain file name.")
    return candidate


def _strip_cached_draft_marker(content: str) -> str:
    lines = content.splitlines()
    if not lines:
        return ""
    if re.fullmatch(r"\s*<!--\s*vaulthalla-release-version:\s*[0-9]+\.[0-9]+\.[0-9]+\s*-->\s*", lines[0]):
        return ("\n".join(lines[1:])).lstrip("\n")
    return content


def _render_ai_compare_profile_config_yaml(pipeline: AIPipelineConfig) -> str:
    lines: list[str] = [
        f"provider: {pipeline.provider}",
        f"profile_slug: {pipeline.profile_slug or '<none>'}",
    ]
    if pipeline.base_url:
        lines.append(f"base_url: {pipeline.base_url}")
    lines.append("enabled_stages:")
    for stage in pipeline.enabled_stages:
        lines.append(f"  - {stage}")

    lines.append("default_max_output_tokens:")
    for stage in ("emergency_triage", "triage", "draft", "polish", "release_notes"):
        token_policy = pipeline.stages[stage].max_output_tokens
        lines.extend(_render_ai_compare_token_policy(stage, token_policy))

    lines.append("stages:")
    for stage in ("emergency_triage", "triage", "draft", "polish", "release_notes"):
        stage_cfg = pipeline.stages[stage]
        lines.append(f"  {stage}:")
        lines.append(f"    model: {stage_cfg.model}")
        lines.append(f"    reasoning_effort: {stage_cfg.reasoning_effort or '<none>'}")
        lines.append(f"    structured_mode: {stage_cfg.structured_mode or '<none>'}")

    return "\n".join(lines) + "\n"


def _render_ai_compare_token_policy(stage: str, policy: object) -> list[str]:
    if isinstance(policy, int):
        return [f"  {stage}: {policy}"]
    mode = getattr(policy, "mode", None)
    ratio = getattr(policy, "ratio", None)
    minimum = getattr(policy, "min", None)
    maximum = getattr(policy, "max", None)
    if mode == "dynamic_ratio" and isinstance(ratio, (int, float)) and isinstance(minimum, int) and isinstance(maximum, int):
        return [
            f"  {stage}:",
            "    mode: dynamic_ratio",
            f"    ratio: {ratio}",
            f"    min: {minimum}",
            f"    max: {maximum}",
        ]
    return [f"  {stage}: {policy!r}"]


def _read_release_notes_for_compare(path: str | None) -> str:
    if not path:
        return "_not generated_"
    candidate = Path(path)
    if not candidate.is_file():
        return "_not generated_"
    content = candidate.read_text(encoding="utf-8").strip()
    if not content:
        return "_not generated_"
    return content


def build_ai_provider_config_from_args(
    args: argparse.Namespace,
    *,
    repo_root: Path | None = None,
    stage: AIStageName = "draft",
) -> AIProviderConfig:
    pipeline = build_ai_pipeline_config_from_args(args, repo_root=repo_root)
    base = pipeline.provider_config_for_stage(stage)
    timeout_seconds = _resolve_stage_provider_timeout_seconds(stage)
    if timeout_seconds is None:
        return base
    return AIProviderConfig(
        kind=base.kind,
        model=base.model,
        base_url=base.base_url,
        api_key_env_var=base.api_key_env_var,
        api_key=base.api_key,
        timeout_seconds=timeout_seconds,
    )


def build_ai_provider_from_config(config: AIProviderConfig) -> StructuredJSONProvider:
    return build_structured_json_provider(config)


def build_ai_provider_from_args(
    args: argparse.Namespace,
    *,
    repo_root: Path | None = None,
    stage: AIStageName = "draft",
) -> StructuredJSONProvider:
    return build_ai_provider_from_config(
        build_ai_provider_config_from_args(args, repo_root=repo_root, stage=stage)
    )


def _resolve_stage_provider_timeout_seconds(stage: AIStageName) -> float | None:
    if stage != "emergency_triage":
        return None
    raw = os.getenv("RELEASE_AI_EMERGENCY_TRIAGE_PROVIDER_TIMEOUT_SECONDS", "45").strip()
    if not raw:
        return 45.0
    try:
        value = float(raw)
    except ValueError:
        return 45.0
    if value <= 0:
        return 45.0
    return value


def _run_stage_preflight(
    *,
    args: argparse.Namespace,
    repo_root: Path,
    draft_provider: StructuredJSONProvider,
    include_emergency_triage: bool,
    include_triage: bool,
    include_polish: bool,
    include_release_notes: bool,
) -> None:
    seen: set[tuple[str, str | None]] = set()
    stage_order: list[AIStageName] = []
    if include_emergency_triage:
        stage_order.append("emergency_triage")
    if include_triage:
        stage_order.append("triage")
    stage_order.append("draft")
    if include_polish:
        stage_order.append("polish")
    if include_release_notes:
        stage_order.append("release_notes")

    for stage in stage_order:
        stage_config = build_ai_provider_config_from_args(args, repo_root=repo_root, stage=stage)
        fingerprint = (stage_config.model, stage_config.base_url)
        if fingerprint in seen:
            continue

        provider = (
            draft_provider
            if stage == "draft"
            else build_ai_provider_from_args(args, repo_root=repo_root, stage=stage)
        )
        _ = run_provider_preflight(stage_config, provider=provider, require_model=True)
        seen.add(fingerprint)


def print_provider_preflight_result(result: ProviderPreflightResult) -> None:
    print("AI provider check")
    print("-----------------")
    print(f"Provider:         {result.provider_kind}")
    print(f"Model:            {result.model}")
    if result.base_url:
        print(f"Base URL:         {result.base_url}")
    print(f"Reachability:     OK")
    print(f"Discovered models:{len(result.discovered_models):>2}")
    if result.discovered_models:
        print(f"Model match:      {'yes' if result.model_found else 'no'}")
        print("Sample models:")
        for model in result.discovered_models[:10]:
            print(f"  - {model}")


def _stage_failure(stage: str, exc: Exception) -> ValueError:
    message = str(exc).strip()
    field = _extract_missing_field(message)
    if field is not None:
        return ValueError(f"{stage} stage failed: missing required field `{field}`. {message}")
    return ValueError(f"{stage} stage failed: {message}")


def _capture_stage_failure_artifact(
    *,
    repo_root: Path,
    args: argparse.Namespace,
    stage: AIStageName,
    pipeline_config: AIPipelineConfig,
    provider: StructuredJSONProvider,
    exc: Exception,
    stage_settings: dict[str, object],
) -> None:
    provider_evidence = collect_provider_failure_evidence(provider)
    if not provider_response_observed(provider_evidence):
        return
    stage_provider_cfg = pipeline_config.provider_config_for_stage(stage)
    mode_value = stage_settings.get("structured_mode")
    if mode_value is None and isinstance(provider_evidence, dict):
        resolved = provider_evidence.get("resolved_settings")
        if isinstance(resolved, dict):
            mode_value = resolved.get("structured_mode")
    try:
        artifact = write_failure_artifact(
            repo_root=repo_root,
            command=getattr(args, "changelog_command", None),
            stage=stage,
            ai_profile=getattr(args, "ai_profile", None),
            provider_key=stage_provider_cfg.kind,
            model=stage_provider_cfg.model,
            structured_mode=str(mode_value or "unknown-mode"),
            normalized_request_settings={
                "stage": stage,
                "provider_kind": stage_provider_cfg.kind,
                "model": stage_provider_cfg.model,
                **stage_settings,
            },
            error=exc,
            provider_evidence=provider_evidence,
        )
    except Exception:
        return
    _print_status(f"Wrote AI failure evidence to {artifact}")


def _extract_missing_field(message: str) -> str | None:
    direct = re.match(r"^`([^`]+)` must be a non-empty string\.$", message)
    if direct:
        return direct.group(1)
    nested = re.match(r"^`([^`]+)\[[0-9]+\]` must be a non-empty string\.$", message)
    if nested:
        return nested.group(1)
    list_field = re.search(r"`([^`]+)` must be a non-empty list of strings\.$", message)
    if list_field:
        return list_field.group(1)
    sections = re.search(r"must include non-empty `([^`]+)` list", message)
    if sections:
        return sections.group(1)
    return None


def _log_release_ai_preflight(settings) -> None:
    _print_status("Release AI preflight")
    _print_status("--------------------")
    _print_status(f"RELEASE_AI_MODE:               {settings.mode}")
    _print_status(f"RELEASE_AI_PROFILE_OPENAI:     {settings.openai_profile}")
    _print_status(f"OPENAI_API_KEY configured:     {'yes' if settings.openai_api_key_present else 'no'}")
    _print_status(f"RELEASE_LOCAL_LLM_ENABLED:     {'true' if settings.local_enabled else 'false'}")
    _print_status(
        f"RELEASE_LOCAL_LLM_PROFILE:     "
        f"{settings.local_profile if settings.local_profile else '<unset>'}"
    )
    _print_status(
        f"RELEASE_LOCAL_LLM_BASE_URL:    "
        f"{settings.local_base_url_override if settings.local_base_url_override else '<unset>'}"
    )
    if settings.mode == "openai-only" and not settings.openai_api_key_present:
        _print_status("Preflight note: openai-only requested but OPENAI_API_KEY is missing; manual fallback may be used.")
    if settings.mode == "local-only" and (not settings.local_enabled or not settings.local_profile):
        _print_status(
            "Preflight note: local-only requested but local fallback is not fully configured; "
            "manual fallback may be used."
        )


def _print_status(line: str) -> None:
    sys.stdout.write(f"{line}\n")
    sys.stdout.flush()
