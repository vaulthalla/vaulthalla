from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

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
from tools.release.changelog.ai.providers import (
    ProviderPreflightResult,
    build_structured_json_provider,
    run_provider_preflight,
)
from tools.release.changelog.ai.providers.base import StructuredJSONProvider
from tools.release.changelog.ai.render.markdown import render_draft_markdown, render_polish_markdown
from tools.release.changelog.ai.stages.draft import generate_draft_from_payload, render_draft_result_json
from tools.release.changelog.ai.stages.polish import render_polish_result_json, run_polish_stage
from tools.release.changelog.ai.stages.triage import render_triage_result_json, run_triage_stage
from tools.release.changelog.release_workflow import (
    parse_release_ai_settings,
    resolve_release_changelog,
)
from tools.release.changelog.context_builder import build_release_context
from tools.release.changelog.payload import build_ai_payload, render_ai_payload_json
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
        default="release/changelog.release.md",
        help="Output markdown path for the selected release changelog path.",
    )
    changelog_release_parser.add_argument(
        "--raw-output",
        default="release/changelog.raw.md",
        help="Output path for deterministic raw changelog evidence.",
    )
    changelog_release_parser.add_argument(
        "--payload-output",
        default="release/changelog.payload.json",
        help="Output path for deterministic AI payload evidence JSON.",
    )
    changelog_release_parser.add_argument(
        "--manual-changelog-path",
        default="debian/changelog",
        help="Manual changelog file used when AI is disabled/unavailable.",
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
        help="Write rendered markdown draft to a file path instead of stdout.",
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
            "Global model override for all AI stages (triage/draft/polish). "
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
    try:
        print("Changelog release stage: build deterministic context + payload")
        context = build_changelog_context(repo_root, args.since_tag)
        payload = build_ai_payload(context)
        raw_markdown = render_release_changelog(context)
        payload_json = render_ai_payload_json(payload)
    except Exception as exc:
        raise ValueError(f"Changelog release failed during context/payload generation: {exc}") from exc

    try:
        print("Changelog release stage: write evidence artifacts")
        write_output(raw_markdown, args.raw_output)
        print(f"Wrote changelog raw evidence to {Path(args.raw_output).resolve()}")
        write_output(payload_json, args.payload_output)
        print(f"Wrote changelog payload evidence to {Path(args.payload_output).resolve()}")
    except Exception as exc:
        raise ValueError(f"Changelog release failed while writing evidence artifacts: {exc}") from exc

    try:
        print("Changelog release stage: resolve AI/manual changelog path")
        env_settings = parse_release_ai_settings(os.environ)
        _log_release_ai_preflight(env_settings)
        selection = resolve_release_changelog(
            repo_root=repo_root,
            payload=payload,
            settings=env_settings,
            manual_changelog_path=args.manual_changelog_path,
            logger=print,
        )
    except Exception as exc:
        raise ValueError(f"Changelog release failed during path selection/generation: {exc}") from exc

    try:
        print("Changelog release stage: write selected release changelog")
        write_output(selection.content, args.output)
        print(f"Wrote release changelog to {Path(args.output).resolve()}")
        if selection.path == "local" and env_settings.local_base_url_override:
            if selection.local_base_url_overrode_profile:
                print("Local base_url override status: applied from RELEASE_LOCAL_LLM_BASE_URL.")
            else:
                print("Local base_url override status: set but not applied.")
    except Exception as exc:
        raise ValueError(f"Changelog release failed while writing selected output: {exc}") from exc
    return 0


def cmd_changelog_ai_draft(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    context = build_changelog_context(repo_root, args.since_tag)
    payload = build_ai_payload(context)
    pipeline_config = build_ai_pipeline_config_from_args(args, repo_root=repo_root)
    run_triage = bool(args.use_triage) or pipeline_config.is_stage_enabled("triage")
    run_polish = bool(args.polish) or pipeline_config.is_stage_enabled("polish")
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
            include_triage=run_triage,
            include_polish=run_polish,
        )

    draft_input: dict = payload
    source_kind = "payload"
    triage_stage_cfg = pipeline_config.stages["triage"]
    draft_stage_cfg = pipeline_config.stages["draft"]
    polish_stage_cfg = pipeline_config.stages["polish"]

    if run_triage:
        triage_provider = build_ai_provider_from_args(args, repo_root=repo_root, stage="triage")
        try:
            triage_result = run_triage_stage(
                payload,
                provider=triage_provider,
                provider_kind=pipeline_config.provider,
                reasoning_effort=triage_stage_cfg.reasoning_effort,
                structured_mode=triage_stage_cfg.structured_mode,
                temperature=triage_stage_cfg.temperature,
                max_output_tokens_policy=triage_stage_cfg.max_output_tokens,
            )
        except Exception as exc:
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
        raise _stage_failure("Draft", exc) from exc
    polish_result: AIPolishResult | None = None

    if run_polish:
        polish_provider = build_ai_provider_from_args(args, repo_root=repo_root, stage="polish")
        try:
            polish_result = run_polish_stage(
                draft,
                provider=polish_provider,
                provider_kind=pipeline_config.provider,
                reasoning_effort=polish_stage_cfg.reasoning_effort,
                structured_mode=polish_stage_cfg.structured_mode,
                temperature=polish_stage_cfg.temperature,
                max_output_tokens_policy=polish_stage_cfg.max_output_tokens,
            )
        except Exception as exc:
            raise _stage_failure("Polish", exc) from exc
        final_markdown = render_polish_markdown(polish_result)

        if args.save_polish_json:
            write_output(render_polish_result_json(polish_result), args.save_polish_json)
            print(f"Wrote AI polish JSON to {Path(args.save_polish_json).resolve()}")
    else:
        final_markdown = render_draft_markdown(draft)

    write_output(final_markdown, args.output)

    if args.output:
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


def build_ai_provider_config_from_args(
    args: argparse.Namespace,
    *,
    repo_root: Path | None = None,
    stage: AIStageName = "draft",
) -> AIProviderConfig:
    pipeline = build_ai_pipeline_config_from_args(args, repo_root=repo_root)
    return pipeline.provider_config_for_stage(stage)


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


def _run_stage_preflight(
    *,
    args: argparse.Namespace,
    repo_root: Path,
    draft_provider: StructuredJSONProvider,
    include_triage: bool,
    include_polish: bool,
) -> None:
    seen: set[tuple[str, str | None]] = set()
    stage_order: list[AIStageName] = []
    if include_triage:
        stage_order.append("triage")
    stage_order.append("draft")
    if include_polish:
        stage_order.append("polish")

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
    print("Release AI preflight")
    print("--------------------")
    print(f"RELEASE_AI_MODE:               {settings.mode}")
    print(f"RELEASE_AI_PROFILE_OPENAI:     {settings.openai_profile}")
    print(f"OPENAI_API_KEY configured:     {'yes' if settings.openai_api_key_present else 'no'}")
    print(f"RELEASE_LOCAL_LLM_ENABLED:     {'true' if settings.local_enabled else 'false'}")
    print(
        f"RELEASE_LOCAL_LLM_PROFILE:     "
        f"{settings.local_profile if settings.local_profile else '<unset>'}"
    )
    print(
        f"RELEASE_LOCAL_LLM_BASE_URL:    "
        f"{settings.local_base_url_override if settings.local_base_url_override else '<unset>'}"
    )
    if settings.mode == "openai-only" and not settings.openai_api_key_present:
        print("Preflight note: openai-only requested but OPENAI_API_KEY is missing; manual fallback may be used.")
    if settings.mode == "local-only" and (not settings.local_enabled or not settings.local_profile):
        print(
            "Preflight note: local-only requested but local fallback is not fully configured; "
            "manual fallback may be used."
        )
