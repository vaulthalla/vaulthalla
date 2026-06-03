from __future__ import annotations

import base64
import gzip
import json
import os
import re
import subprocess
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Mapping
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from tools.release.packaging.publication import redact_url
from tools.release.version.models import Version

RELEASE_BASE_RESOLUTION_SCHEMA_VERSION = "vaulthalla.release.base_resolution.v1"
RELEASE_SUCCESS_LEDGER_SCHEMA_VERSION = "vaulthalla.release.success_ledger.v1"
DEFAULT_RELEASE_SUCCESS_LEDGER = "vaulthalla-release-success.json"

HttpGet = Callable[[str, Mapping[str, str]], bytes]


@dataclass(frozen=True)
class SkippedReleaseTag:
    tag: str
    reason: str


@dataclass(frozen=True)
class ReleaseBaseResolution:
    requested_version: str
    latest_git_tag: str | None
    last_successful_release_tag: str
    source: str
    commit_range: str
    skipped_release_tags: tuple[SkippedReleaseTag, ...] = ()
    apt_repository_url: str | None = None
    apt_repository_label: str | None = None
    expected_debian_packages: tuple[str, ...] = ()

    def to_dict(self) -> dict[str, Any]:
        data = asdict(self)
        data["schema_version"] = RELEASE_BASE_RESOLUTION_SCHEMA_VERSION
        data["skipped_release_tags"] = [asdict(item) for item in self.skipped_release_tags]
        return data


@dataclass(frozen=True)
class ReleaseSuccessRecord:
    version: str
    tag: str
    head_sha: str
    package_artifacts: tuple[str, ...]
    package_names: tuple[str, ...]
    publication_mode: str
    target_url: str | None
    workflow_run_id: str | None
    created_at: str

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class AptMetadataConfig:
    repository_url: str | None
    suite: str
    components: tuple[str, ...]
    architectures: tuple[str, ...]
    expected_packages: tuple[str, ...]
    username: str | None
    password: str | None


def resolve_release_notes_base(
    *,
    repo_root: Path | str,
    version: str,
    override_tag: str | None = None,
    env: Mapping[str, str] | None = None,
    http_get: HttpGet | None = None,
) -> ReleaseBaseResolution:
    root = Path(repo_root).resolve()
    environment = os.environ if env is None else env
    target = Version.parse(str(version))
    latest_tag = _get_latest_tag(root)
    candidates = _list_lower_semver_tags(root, target)
    skipped: list[SkippedReleaseTag] = []
    getter = _default_http_get if http_get is None else http_get

    explicit_override = _normalize_optional_tag(override_tag or environment.get("RELEASE_NOTES_BASE"))
    if explicit_override is not None:
        _validate_base_tag(explicit_override, target)
        for candidate in candidates:
            if candidate == explicit_override:
                break
            skipped.append(SkippedReleaseTag(candidate, f"operator override selected {explicit_override}"))
        return _resolution(
            version=version,
            latest_git_tag=latest_tag,
            tag=explicit_override,
            source="operator-override",
            skipped=skipped,
            apt_config=_apt_metadata_config(root, environment),
        )

    ledger_tags = _read_success_ledger_tags(root, environment)
    if ledger_tags:
        for candidate in candidates:
            if candidate in ledger_tags:
                return _resolution(
                    version=version,
                    latest_git_tag=latest_tag,
                    tag=candidate,
                    source="release-ledger",
                    skipped=skipped,
                    apt_config=_apt_metadata_config(root, environment),
                )
            skipped.append(SkippedReleaseTag(candidate, "not recorded in release success ledger"))

    apt_config = _apt_metadata_config(root, environment)
    apt_required = _apt_success_required(environment)
    apt_available = bool(apt_config.repository_url)
    if apt_available:
        apt_metadata = _load_apt_metadata(apt_config, http_get=getter)
        for candidate in candidates:
            candidate_version = _tag_to_version_string(candidate)
            missing = _missing_expected_apt_packages(
                apt_metadata=apt_metadata,
                version=candidate_version,
                expected_packages=apt_config.expected_packages,
            )
            if not missing:
                return _resolution(
                    version=version,
                    latest_git_tag=latest_tag,
                    tag=candidate,
                    source="nexus-apt-metadata",
                    skipped=skipped,
                    apt_config=apt_config,
                )
            skipped.append(
                SkippedReleaseTag(
                    candidate,
                    "APT metadata missing expected package set: " + ", ".join(missing),
                )
            )
    elif apt_required:
        raise ValueError(
            "Cannot determine shipped release baseline: Debian/APT publication is required, "
            "but no APT repository URL is configured. Set RELEASE_APT_REPOSITORY_URL or RELEASE_NOTES_BASE."
        )

    weaker_sources_allowed = _github_only_success_allowed(environment) or not apt_required
    if weaker_sources_allowed:
        for candidate in candidates:
            if _github_release_has_assets(candidate, environment, http_get=getter):
                return _resolution(
                    version=version,
                    latest_git_tag=latest_tag,
                    tag=candidate,
                    source="github-release-metadata",
                    skipped=skipped,
                    apt_config=apt_config,
                )
            skipped.append(SkippedReleaseTag(candidate, "GitHub release metadata not available or incomplete"))

        for candidate in candidates:
            if _release_workflow_succeeded(candidate, environment, http_get=getter):
                return _resolution(
                    version=version,
                    latest_git_tag=latest_tag,
                    tag=candidate,
                    source="workflow-run",
                    skipped=skipped,
                    apt_config=apt_config,
                )
            skipped.append(SkippedReleaseTag(candidate, "no successful release workflow run found"))

    rendered_skips = "; ".join(f"{item.tag}: {item.reason}" for item in skipped) or "no lower semver tags found"
    raise ValueError(
        "Cannot determine shipped release baseline. Refusing to fall back to latest git tag. "
        f"Skipped candidates: {rendered_skips}. Set RELEASE_NOTES_BASE to an explicit shipped release tag."
    )


def render_release_base_resolution_json(resolution: ReleaseBaseResolution) -> str:
    return json.dumps(resolution.to_dict(), indent=2) + "\n"


def render_release_base_summary(resolution: ReleaseBaseResolution) -> str:
    lines = [
        "Release notes base preflight",
        "----------------------------",
        f"Requested version:                {resolution.requested_version}",
        f"Latest git tag:                   {resolution.latest_git_tag or '<none>'}",
        f"Last successful shipped release:  {resolution.last_successful_release_tag}",
        f"Success source:                   {resolution.source}",
        f"Commit range:                     {resolution.commit_range}",
    ]
    if resolution.apt_repository_label:
        lines.append(f"APT/Nexus target:                 {resolution.apt_repository_label}")
    if resolution.expected_debian_packages:
        lines.append("Expected Debian packages:         " + ", ".join(resolution.expected_debian_packages))
    if resolution.skipped_release_tags:
        lines.append("Skipped release tags:")
        for item in resolution.skipped_release_tags:
            lines.append(f"- {item.tag}: {item.reason}")
    else:
        lines.append("Skipped release tags:             none")
    return "\n".join(lines) + "\n"


def record_release_success(
    *,
    repo_root: Path | str,
    output_dir: Path | str,
    tag: str,
    version: str,
    publication_mode: str,
    target_url: str | None = None,
    workflow_run_id: str | None = None,
    output: Path | str | None = None,
) -> Path:
    root = Path(repo_root).resolve()
    release_dir = Path(output_dir)
    if not release_dir.is_absolute():
        release_dir = (root / release_dir).resolve()
    if not release_dir.is_dir():
        raise ValueError(f"Cannot record release success: output directory does not exist: {release_dir}")

    debs = tuple(sorted(path.name for path in release_dir.iterdir() if path.is_file() and path.name.endswith(".deb")))
    if not debs:
        raise ValueError(f"Cannot record release success: no Debian package artifacts found under {release_dir}")

    record = ReleaseSuccessRecord(
        version=version,
        tag=tag,
        head_sha=_get_head_sha(root),
        package_artifacts=debs,
        package_names=tuple(sorted({_package_name_from_deb_name(name) for name in debs})),
        publication_mode=publication_mode,
        target_url=redact_url(target_url) if target_url else None,
        workflow_run_id=workflow_run_id,
        created_at=datetime.now(timezone.utc).isoformat(),
    )
    payload = {
        "schema_version": RELEASE_SUCCESS_LEDGER_SCHEMA_VERSION,
        "releases": [record.to_dict()],
    }

    target = Path(output) if output is not None else release_dir / DEFAULT_RELEASE_SUCCESS_LEDGER
    if not target.is_absolute():
        target = (root / target).resolve()
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return target


def _resolution(
    *,
    version: str,
    latest_git_tag: str | None,
    tag: str,
    source: str,
    skipped: list[SkippedReleaseTag],
    apt_config: AptMetadataConfig,
) -> ReleaseBaseResolution:
    return ReleaseBaseResolution(
        requested_version=str(version),
        latest_git_tag=latest_git_tag,
        last_successful_release_tag=tag,
        source=source,
        commit_range=f"{tag}..HEAD",
        skipped_release_tags=tuple(skipped),
        apt_repository_url=redact_url(apt_config.repository_url) if apt_config.repository_url else None,
        apt_repository_label=redact_url(apt_config.repository_url) if apt_config.repository_url else None,
        expected_debian_packages=apt_config.expected_packages,
    )


def _validate_base_tag(tag: str, target: Version) -> None:
    version = Version.parse(_tag_to_version_string(tag))
    if version >= target:
        raise ValueError(
            f"Release notes base `{tag}` must be lower than requested VERSION {target}; "
            "refusing to build an empty or forward release range."
        )


def _normalize_optional_tag(raw: str | None) -> str | None:
    if raw is None:
        return None
    tag = raw.strip()
    return tag or None


def _tag_to_version_string(tag: str) -> str:
    normalized = tag.strip()
    return normalized[1:] if normalized.startswith("v") else normalized


def _list_lower_semver_tags(repo_root: Path, target: Version) -> list[str]:
    try:
        output = _run_git(["tag", "--merged", "HEAD", "--list"], repo_root)
    except RuntimeError:
        return []
    parsed: list[tuple[Version, str]] = []
    for raw in output.splitlines():
        tag = raw.strip()
        if not tag:
            continue
        try:
            version = Version.parse(_tag_to_version_string(tag))
        except ValueError:
            continue
        if version < target:
            parsed.append((version, tag))
    parsed.sort(key=lambda item: item[0], reverse=True)
    return [tag for _, tag in parsed]


def _get_latest_tag(repo_root: Path) -> str | None:
    try:
        result = _run_git(["describe", "--tags", "--abbrev=0"], repo_root).strip()
    except RuntimeError:
        return None
    return result or None


def _get_head_sha(repo_root: Path) -> str:
    return _run_git(["rev-parse", "HEAD"], repo_root).strip()


def _run_git(args: list[str], repo_root: Path) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=repo_root,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout


def _read_success_ledger_tags(repo_root: Path, env: Mapping[str, str]) -> set[str]:
    tags: set[str] = set()
    for path in _success_ledger_paths(repo_root, env):
        if not path.is_file():
            continue
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            continue
        records: list[Any]
        if isinstance(data, dict) and isinstance(data.get("releases"), list):
            records = data["releases"]
        elif isinstance(data, dict):
            records = [data]
        else:
            records = []
        for item in records:
            if not isinstance(item, dict):
                continue
            tag = item.get("tag")
            if isinstance(tag, str) and tag.strip():
                tags.add(tag.strip())
    return tags


def _success_ledger_paths(repo_root: Path, env: Mapping[str, str]) -> tuple[Path, ...]:
    configured = env.get("RELEASE_SUCCESS_LEDGER_PATH")
    if configured:
        candidate = Path(configured)
        return ((repo_root / candidate).resolve() if not candidate.is_absolute() else candidate.resolve(),)
    return (
        repo_root / DEFAULT_RELEASE_SUCCESS_LEDGER,
        repo_root / "release" / DEFAULT_RELEASE_SUCCESS_LEDGER,
    )


def _apt_metadata_config(repo_root: Path, env: Mapping[str, str]) -> AptMetadataConfig:
    repository_url = _read_optional_env(env, "RELEASE_APT_REPOSITORY_URL") or _read_optional_env(env, "NEXUS_REPO_URL")
    suite = _read_optional_env(env, "RELEASE_APT_SUITE") or "stable"
    components = _split_env_list(env.get("RELEASE_APT_COMPONENTS"), default=("main",))
    architectures = _split_env_list(env.get("RELEASE_APT_ARCHITECTURES"), default=("amd64",))
    expected = _split_env_list(env.get("RELEASE_EXPECTED_DEBIAN_PACKAGES"), default=())
    if not expected:
        expected = _read_binary_package_names(repo_root)
    return AptMetadataConfig(
        repository_url=repository_url,
        suite=suite,
        components=components,
        architectures=architectures,
        expected_packages=expected,
        username=_read_optional_env(env, "NEXUS_USER"),
        password=_read_optional_env(env, "NEXUS_PASS"),
    )


def _read_optional_env(env: Mapping[str, str], key: str) -> str | None:
    raw = env.get(key)
    if raw is None:
        return None
    normalized = raw.strip()
    return normalized or None


def _split_env_list(raw: str | None, *, default: tuple[str, ...]) -> tuple[str, ...]:
    if raw is None or not raw.strip():
        return default
    values = tuple(item.strip() for item in re.split(r"[,\s]+", raw) if item.strip())
    return values or default


def _read_binary_package_names(repo_root: Path) -> tuple[str, ...]:
    control = repo_root / "debian" / "control"
    if not control.is_file():
        return ("vaulthalla",)
    names: list[str] = []
    for line in control.read_text(encoding="utf-8").splitlines():
        if line.startswith("Package:"):
            name = line.split(":", 1)[1].strip()
            if name:
                names.append(name)
    return tuple(sorted(set(names))) or ("vaulthalla",)


def _apt_success_required(env: Mapping[str, str]) -> bool:
    if _parse_bool(env.get("RELEASE_REQUIRE_APT_SUCCESS"), default=False):
        return True
    if (env.get("RELEASE_PUBLISH_MODE") or "").strip().lower() == "nexus":
        return True
    return False


def _github_only_success_allowed(env: Mapping[str, str]) -> bool:
    return _parse_bool(env.get("RELEASE_SUCCESS_ALLOW_GITHUB_ONLY"), default=False)


def _parse_bool(raw: str | None, *, default: bool) -> bool:
    if raw is None or not raw.strip():
        return default
    return raw.strip().lower() in {"1", "true", "yes", "on"}


def _load_apt_metadata(config: AptMetadataConfig, *, http_get: HttpGet) -> dict[str, set[str]]:
    if not config.repository_url:
        return {}
    headers = _auth_headers(config)
    errors: list[str] = []
    merged: dict[str, set[str]] = {}
    for url in _apt_package_index_urls(config):
        try:
            content = http_get(url, headers)
            if url.endswith(".gz"):
                content = gzip.decompress(content)
        except Exception as exc:
            errors.append(f"{redact_url(url)} ({exc})")
            continue
        _merge_apt_packages(merged, _parse_apt_packages(content.decode("utf-8", errors="replace")))
    if not merged:
        rendered = "; ".join(errors) if errors else "no package indexes were attempted"
        raise ValueError(f"Unable to read APT package metadata from configured repository: {rendered}")
    return merged


def _auth_headers(config: AptMetadataConfig) -> dict[str, str]:
    if not config.username or not config.password:
        return {}
    token = base64.b64encode(f"{config.username}:{config.password}".encode("utf-8")).decode("ascii")
    return {"Authorization": f"Basic {token}"}


def _apt_package_index_urls(config: AptMetadataConfig) -> tuple[str, ...]:
    assert config.repository_url is not None
    base = config.repository_url.rstrip("/")
    if base.endswith("/Packages") or base.endswith("/Packages.gz"):
        return (base,)
    urls: list[str] = []
    for component in config.components:
        for arch in config.architectures:
            index_base = f"{base}/dists/{config.suite}/{component}/binary-{arch}/Packages"
            urls.append(f"{index_base}.gz")
            urls.append(index_base)
    return tuple(urls)


def _parse_apt_packages(content: str) -> dict[str, set[str]]:
    packages: dict[str, set[str]] = {}
    for stanza in re.split(r"\n\s*\n", content):
        fields: dict[str, str] = {}
        current_key: str | None = None
        for line in stanza.splitlines():
            if not line.strip():
                continue
            if line.startswith((" ", "\t")) and current_key:
                fields[current_key] += "\n" + line.strip()
                continue
            if ":" not in line:
                continue
            key, value = line.split(":", 1)
            current_key = key.strip()
            fields[current_key] = value.strip()
        name = fields.get("Package")
        version = fields.get("Version")
        if not name or not version:
            continue
        packages.setdefault(name, set()).add(version)
    return packages


def _merge_apt_packages(target: dict[str, set[str]], source: dict[str, set[str]]) -> None:
    for name, versions in source.items():
        target.setdefault(name, set()).update(versions)


def _missing_expected_apt_packages(
    *,
    apt_metadata: dict[str, set[str]],
    version: str,
    expected_packages: tuple[str, ...],
) -> list[str]:
    missing: list[str] = []
    for package in expected_packages:
        versions = apt_metadata.get(package, set())
        if not any(_debian_upstream_version(candidate) == version for candidate in versions):
            missing.append(package)
    return missing


def _debian_upstream_version(version: str) -> str:
    if ":" in version:
        version = version.split(":", 1)[1]
    if "-" in version:
        version = version.rsplit("-", 1)[0]
    return version


def _github_release_has_assets(tag: str, env: Mapping[str, str], *, http_get: HttpGet) -> bool:
    repo = _github_repository(env)
    token = _github_token(env)
    if not repo or not token:
        return False
    url = f"https://api.github.com/repos/{repo}/releases/tags/{tag}"
    try:
        data = json.loads(http_get(url, _github_headers(token)).decode("utf-8"))
    except Exception:
        return False
    if not isinstance(data, dict):
        return False
    if data.get("draft") or data.get("prerelease"):
        return False
    assets = data.get("assets")
    return isinstance(assets, list) and bool(assets)


def _release_workflow_succeeded(tag: str, env: Mapping[str, str], *, http_get: HttpGet) -> bool:
    repo = _github_repository(env)
    token = _github_token(env)
    workflow = (env.get("RELEASE_WORKFLOW_NAME") or "release.yml").strip()
    if not repo or not token or not workflow:
        return False
    url = (
        f"https://api.github.com/repos/{repo}/actions/workflows/{workflow}/runs"
        f"?branch={tag}&event=push&status=success&per_page=20"
    )
    try:
        data = json.loads(http_get(url, _github_headers(token)).decode("utf-8"))
    except Exception:
        return False
    runs = data.get("workflow_runs") if isinstance(data, dict) else None
    if not isinstance(runs, list):
        return False
    return any(isinstance(item, dict) and item.get("conclusion") == "success" for item in runs)


def _github_repository(env: Mapping[str, str]) -> str | None:
    return _read_optional_env(env, "GITHUB_REPOSITORY") or _read_optional_env(env, "RELEASE_GITHUB_REPOSITORY")


def _github_token(env: Mapping[str, str]) -> str | None:
    return _read_optional_env(env, "GITHUB_TOKEN") or _read_optional_env(env, "GH_TOKEN")


def _github_headers(token: str) -> dict[str, str]:
    return {
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
    }


def _default_http_get(url: str, headers: Mapping[str, str]) -> bytes:
    request = Request(url, headers=dict(headers))
    try:
        with urlopen(request, timeout=20) as response:
            return response.read()
    except HTTPError as exc:
        raise ValueError(f"HTTP {exc.code}") from exc
    except URLError as exc:
        raise ValueError(str(exc.reason)) from exc


def _package_name_from_deb_name(name: str) -> str:
    if "_" in name:
        return name.split("_", 1)[0]
    return name.removesuffix(".deb")
