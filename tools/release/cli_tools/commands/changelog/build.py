import argparse
import json
import os
from pathlib import Path

from tools.release.changelog import build_release_context
from tools.release.changelog.ai import AIPipelineConfig, AIPipelineCLIOverrides, resolve_ai_pipeline_config, \
    AIStageName, AIProviderConfig, build_structured_json_provider
from tools.release.changelog.ai.providers import StructuredJSONProvider
from tools.release.version.adapters import read_version_file


def build_changelog_context(
    repo_root: Path,
    since_tag: str | None,
    *,
    release_notes_base: str | None = None,
    release_notes_base_resolution: str | None = None,
):
    version_path = repo_root / "VERSION"

    try:
        version = read_version_file(version_path)
    except Exception as exc:
        raise ValueError(f"Failed to read canonical version from {version_path}: {exc}") from exc

    previous_tag = _resolve_release_notes_base_arg(
        since_tag=since_tag,
        release_notes_base=release_notes_base,
    )
    resolution_metadata = _read_release_notes_base_resolution(
        repo_root=repo_root,
        path=release_notes_base_resolution,
    )
    if resolution_metadata:
        resolved_tag = resolution_metadata.get("last_successful_release_tag")
        if previous_tag is None and isinstance(resolved_tag, str) and resolved_tag.strip():
            previous_tag = resolved_tag.strip()
        elif isinstance(resolved_tag, str) and resolved_tag.strip() and previous_tag != resolved_tag.strip():
            raise ValueError(
                "Release notes base mismatch: CLI base "
                f"`{previous_tag}` does not match resolution metadata `{resolved_tag.strip()}`."
            )

    return build_release_context(
        version=str(version),
        repo_root=repo_root,
        previous_tag=previous_tag,
        release_success_source=_read_resolution_source(resolution_metadata, previous_tag),
        skipped_release_tags=_read_resolution_skipped_tags(resolution_metadata),
        skip_reasons=_read_resolution_skip_reasons(resolution_metadata),
    )


def _resolve_release_notes_base_arg(
    *,
    since_tag: str | None,
    release_notes_base: str | None,
) -> str | None:
    since = since_tag.strip() if isinstance(since_tag, str) and since_tag.strip() else None
    base = release_notes_base.strip() if isinstance(release_notes_base, str) and release_notes_base.strip() else None
    if since and base and since != base:
        raise ValueError(
            f"Conflicting changelog range lower bounds: --since-tag={since} "
            f"but --release-notes-base={base}."
        )
    return base or since


def _read_release_notes_base_resolution(repo_root: Path, path: str | None) -> dict | None:
    if not path:
        return None
    target = Path(path)
    if not target.is_absolute():
        target = (repo_root / target).resolve()
    if not target.is_file():
        raise ValueError(f"Release notes base resolution metadata not found: {target}")
    data = json.loads(target.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"Release notes base resolution metadata is not an object: {target}")
    return data


def _read_resolution_source(metadata: dict | None, previous_tag: str | None) -> str | None:
    if metadata:
        raw = metadata.get("source")
        if isinstance(raw, str) and raw.strip():
            return raw.strip()
    if previous_tag is not None:
        return "explicit-range"
    return None


def _read_resolution_skipped_tags(metadata: dict | None) -> list[str]:
    if not metadata:
        return []
    skipped = metadata.get("skipped_release_tags")
    if not isinstance(skipped, list):
        return []
    tags: list[str] = []
    for item in skipped:
        if isinstance(item, dict):
            tag = item.get("tag")
            if isinstance(tag, str) and tag.strip():
                tags.append(tag.strip())
        elif isinstance(item, str) and item.strip():
            tags.append(item.strip())
    return tags


def _read_resolution_skip_reasons(metadata: dict | None) -> dict[str, str]:
    if not metadata:
        return {}
    skipped = metadata.get("skipped_release_tags")
    if not isinstance(skipped, list):
        return {}
    reasons: dict[str, str] = {}
    for item in skipped:
        if not isinstance(item, dict):
            continue
        tag = item.get("tag")
        reason = item.get("reason")
        if isinstance(tag, str) and tag.strip() and isinstance(reason, str) and reason.strip():
            reasons[tag.strip()] = reason.strip()
    return reasons


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
