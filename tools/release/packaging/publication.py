from __future__ import annotations

import json
import os
import re
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Mapping
from urllib.parse import urlparse, urlunparse

from tools.release.packaging.apt_index import (
    AptIndex,
    AptIndexConfig,
    compare_debian_versions,
    load_apt_index,
)
from tools.release.packaging.checksums import SHA256SUMS_NAME, read_sha256sums, sha256_file

DEFAULT_PUBLICATION_MODE = "disabled"
SUPPORTED_PUBLICATION_MODES: tuple[str, ...] = ("disabled", "nexus")
# Nexus can take several minutes to reindex after an upload (v1.7.0: >2.5 min), so poll for ~10 minutes.
DEFAULT_VERIFY_ATTEMPTS = 40
DEFAULT_VERIFY_DELAY_SECONDS = 15.0
LAB_SMOKE_EVIDENCE_SCHEMA = "vaulthalla.release.lab_smoke.v1"
_DEB_NAME_PATTERN = re.compile(r"^(?P<package>[a-z0-9][a-z0-9+.-]*)_(?P<version>[^_]+)_(?P<arch>[a-z0-9-]+)\.deb$")

ACTION_UPLOAD = "upload"
ACTION_SKIP_IDENTICAL = "skip-identical"

Uploader = Callable[[Path, str, str, str], None]
IndexLoader = Callable[[], AptIndex]


class PublicationIntegrityError(ValueError):
    """Raised when publishing would replace or contradict bytes already in the APT repository."""


@dataclass(frozen=True)
class DebianPublicationSettings:
    mode: str
    nexus_repo_url: str
    nexus_user: str
    nexus_password: str
    apt_repository_url: str = ""
    apt_suite: str = "stable"
    apt_components: tuple[str, ...] = ("main",)
    apt_architectures: tuple[str, ...] = ("amd64",)

    def apt_index_config(self) -> AptIndexConfig:
        url = self.apt_repository_url or self.nexus_repo_url
        if not url:
            raise ValueError(
                "Cannot check the APT repository before publishing: set RELEASE_APT_REPOSITORY_URL "
                "(or NEXUS_REPO_URL)."
            )
        return AptIndexConfig(
            repository_url=url,
            suite=self.apt_suite,
            components=self.apt_components,
            architectures=self.apt_architectures,
            username=self.nexus_user or None,
            password=self.nexus_password or None,
        )


@dataclass(frozen=True)
class DebianArtifactIdentity:
    path: Path
    package: str
    version: str
    architecture: str
    sha256: str


@dataclass(frozen=True)
class DebianPublicationPlan:
    artifact: DebianArtifactIdentity
    action: str
    reason: str


@dataclass(frozen=True)
class DebianPublicationResult:
    output_dir: Path
    mode: str
    enabled: bool
    dry_run: bool
    artifacts: tuple[Path, ...]
    target_urls: tuple[str, ...]
    skipped_reason: str | None = None
    plans: tuple[DebianPublicationPlan, ...] = field(default_factory=tuple)
    verified: bool = False

    @property
    def uploaded(self) -> tuple[Path, ...]:
        if self.dry_run:
            return ()
        return tuple(plan.artifact.path for plan in self.plans if plan.action == ACTION_UPLOAD)


def resolve_debian_publication_settings(
    *,
    mode: str | None = None,
    nexus_repo_url: str | None = None,
    nexus_user: str | None = None,
    nexus_password: str | None = None,
    apt_repository_url: str | None = None,
    env: Mapping[str, str] | None = None,
) -> DebianPublicationSettings:
    environment = os.environ if env is None else env

    resolved_mode = (mode or environment.get("RELEASE_PUBLISH_MODE", DEFAULT_PUBLICATION_MODE)).strip().lower()
    if resolved_mode not in SUPPORTED_PUBLICATION_MODES:
        supported = ", ".join(SUPPORTED_PUBLICATION_MODES)
        raise ValueError(f"Unsupported release publication mode `{resolved_mode}`. Supported values: {supported}.")

    resolved_repo_url = (nexus_repo_url or environment.get("NEXUS_REPO_URL", "")).strip()
    resolved_user = (nexus_user or environment.get("NEXUS_USER", "")).strip()
    resolved_password = (nexus_password or environment.get("NEXUS_PASS", "")).strip()
    resolved_apt_url = (apt_repository_url or environment.get("RELEASE_APT_REPOSITORY_URL", "")).strip()

    if resolved_mode == "nexus":
        missing: list[str] = []
        if not resolved_repo_url:
            missing.append("NEXUS_REPO_URL")
        if not resolved_user:
            missing.append("NEXUS_USER")
        if not resolved_password:
            missing.append("NEXUS_PASS")
        if missing:
            rendered = ", ".join(missing)
            raise ValueError(
                "Debian publication mode is `nexus` but required Nexus configuration is missing: "
                f"{rendered}."
            )
        _validate_nexus_repo_url(resolved_repo_url)
        if resolved_apt_url:
            _validate_nexus_repo_url(resolved_apt_url, name="RELEASE_APT_REPOSITORY_URL")

    return DebianPublicationSettings(
        mode=resolved_mode,
        nexus_repo_url=resolved_repo_url,
        nexus_user=resolved_user,
        nexus_password=resolved_password,
        apt_repository_url=resolved_apt_url,
        apt_suite=(environment.get("RELEASE_APT_SUITE") or "stable").strip() or "stable",
        apt_components=_split_env_list(environment.get("RELEASE_APT_COMPONENTS"), default=("main",)),
        apt_architectures=_split_env_list(environment.get("RELEASE_APT_ARCHITECTURES"), default=("amd64",)),
    )


def select_debian_publication_artifacts(*, output_dir: Path | str) -> tuple[Path, ...]:
    destination = Path(output_dir).resolve()
    if not destination.is_dir():
        raise ValueError(
            "Debian publication failed: output directory does not exist "
            f"or is not a directory: {destination}"
        )

    artifacts = _find_debian_publication_artifacts(destination)
    if not artifacts:
        raise ValueError(
            "Debian publication failed: no Debian package artifacts were found "
            f"under {destination} (*.deb)."
        )
    return artifacts


def identify_debian_artifact(path: Path) -> DebianArtifactIdentity:
    """Identity from the canonical dpkg-buildpackage filename (`<pkg>_<version>_<arch>.deb`)."""
    match = _DEB_NAME_PATTERN.fullmatch(path.name)
    if not match:
        raise ValueError(
            f"Debian publication failed: {path.name} is not a canonical `<package>_<version>_<arch>.deb` name."
        )
    return DebianArtifactIdentity(
        path=path,
        package=match.group("package"),
        version=match.group("version"),
        architecture=match.group("arch"),
        sha256=sha256_file(path),
    )


def plan_debian_publication(
    artifacts: tuple[DebianArtifactIdentity, ...],
    index: AptIndex,
    *,
    allow_older_version: bool = False,
) -> tuple[DebianPublicationPlan, ...]:
    """Decide per artifact: upload (absent), skip (identical bytes live), or fail (different bytes live)."""
    plans: list[DebianPublicationPlan] = []
    for artifact in artifacts:
        published = index.lookup(artifact.package, artifact.version, artifact.architecture)
        if published:
            digests = {entry.sha256 for entry in published}
            if None in digests:
                raise PublicationIntegrityError(
                    f"{artifact.package} {artifact.version} ({artifact.architecture}) is already published, but the "
                    "APT index carries no SHA256 for it, so identical bytes cannot be proven. Refusing to re-upload."
                )
            if digests != {artifact.sha256}:
                rendered = ", ".join(sorted(str(item) for item in digests))
                raise PublicationIntegrityError(
                    f"REFUSING TO OVERWRITE A PUBLISHED VERSION: {artifact.package} {artifact.version} "
                    f"({artifact.architecture}) is already in the APT repository with sha256 {rendered}, but the "
                    f"candidate {artifact.path.name} has sha256 {artifact.sha256}. A published version is immutable: "
                    "cut a new version (bump patch or the Debian revision) instead of re-publishing this one. "
                    "If this is a pipeline re-run, re-run only the failed jobs so the originally built artifact is reused."
                )
            plans.append(
                DebianPublicationPlan(
                    artifact=artifact,
                    action=ACTION_SKIP_IDENTICAL,
                    reason="already published with identical sha256",
                )
            )
            continue

        newest = index.newest_version(artifact.package)
        if newest is not None and compare_debian_versions(artifact.version, newest) < 0 and not allow_older_version:
            raise PublicationIntegrityError(
                f"Refusing to publish {artifact.package} {artifact.version}: the APT repository already carries the "
                f"newer version {newest}. Pass --allow-older-version only for a deliberate out-of-order release."
            )
        plans.append(DebianPublicationPlan(artifact=artifact, action=ACTION_UPLOAD, reason="version not yet published"))
    return tuple(plans)


def verify_debian_publication(
    artifacts: tuple[DebianArtifactIdentity, ...],
    *,
    index_loader: IndexLoader,
    attempts: int = DEFAULT_VERIFY_ATTEMPTS,
    delay_seconds: float = DEFAULT_VERIFY_DELAY_SECONDS,
    sleep: Callable[[float], None] = time.sleep,
    logger: Callable[[str], None] = print,
) -> None:
    """Poll the APT index until every artifact is listed with the expected SHA256."""
    attempts = max(1, attempts)
    last_problem = "no attempts made"
    for attempt in range(1, attempts + 1):
        try:
            index = index_loader()
        except Exception as exc:
            last_problem = f"index unreadable: {exc}"
        else:
            pending: list[str] = []
            for artifact in artifacts:
                published = index.lookup(artifact.package, artifact.version, artifact.architecture)
                if not published:
                    pending.append(f"{artifact.package} {artifact.version} ({artifact.architecture}) not listed yet")
                    continue
                digests = {entry.sha256 for entry in published}
                if digests != {artifact.sha256}:
                    rendered = ", ".join(sorted(str(item) for item in digests))
                    raise PublicationIntegrityError(
                        f"Published APT metadata for {artifact.package} {artifact.version} lists sha256 {rendered}, "
                        f"expected {artifact.sha256} ({artifact.path.name})."
                    )
            if not pending:
                logger(
                    "[publish] verified APT index lists "
                    + ", ".join(f"{a.package} {a.version} sha256={a.sha256}" for a in artifacts)
                )
                return
            last_problem = "; ".join(pending)
        logger(f"[publish] APT verification attempt {attempt}/{attempts}: {last_problem}")
        if attempt < attempts:
            sleep(delay_seconds)
    raise ValueError(f"Published APT metadata did not confirm the release after {attempts} attempts: {last_problem}")


def verify_against_sha256sums(
    output_dir: Path,
    artifacts: tuple[DebianArtifactIdentity, ...],
    *,
    required: bool,
) -> None:
    sums_path = output_dir / SHA256SUMS_NAME
    if not sums_path.is_file():
        if required:
            raise PublicationIntegrityError(
                f"{SHA256SUMS_NAME} is missing under {output_dir}; refusing to publish unverified artifacts."
            )
        return
    entries = read_sha256sums(sums_path)
    for artifact in artifacts:
        expected = entries.get(artifact.path.name)
        if expected is None:
            raise PublicationIntegrityError(f"{artifact.path.name} is not listed in {SHA256SUMS_NAME}.")
        if expected != artifact.sha256:
            raise PublicationIntegrityError(
                f"{artifact.path.name} sha256 {artifact.sha256} does not match {SHA256SUMS_NAME} ({expected}); "
                "the artifact changed after it was built and validated."
            )


def verify_against_lab_evidence(evidence_path: Path, artifacts: tuple[DebianArtifactIdentity, ...]) -> None:
    try:
        data = json.loads(evidence_path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise PublicationIntegrityError(f"Cannot read lab smoke evidence {evidence_path}: {exc}") from exc
    if not isinstance(data, dict) or data.get("schema_version") != LAB_SMOKE_EVIDENCE_SCHEMA:
        raise PublicationIntegrityError(f"{evidence_path} is not {LAB_SMOKE_EVIDENCE_SCHEMA} evidence.")
    if data.get("ok") is not True:
        raise PublicationIntegrityError(f"Lab smoke evidence {evidence_path} reports failure; refusing to publish.")
    candidate = data.get("candidate") if isinstance(data.get("candidate"), dict) else {}
    tested = str(candidate.get("sha256") or "")
    for artifact in artifacts:
        if artifact.sha256 != tested:
            raise PublicationIntegrityError(
                f"{artifact.path.name} sha256 {artifact.sha256} is not the package the lab smoke tested "
                f"({tested or '<none>'})."
            )


def publish_debian_artifacts(
    *,
    output_dir: Path | str,
    settings: DebianPublicationSettings,
    dry_run: bool = False,
    require_enabled: bool = False,
    uploader: Uploader | None = None,
    index_loader: IndexLoader | None = None,
    allow_older_version: bool = False,
    lab_evidence: Path | str | None = None,
    verify_attempts: int = DEFAULT_VERIFY_ATTEMPTS,
    verify_delay_seconds: float = DEFAULT_VERIFY_DELAY_SECONDS,
    sleep: Callable[[float], None] = time.sleep,
    logger: Callable[[str], None] = print,
) -> DebianPublicationResult:
    """Idempotent, integrity-checked publication.

    Per `.deb`: version absent from the APT index -> upload; present with identical SHA256 -> skip;
    present with different SHA256 -> PublicationIntegrityError. After uploading, the index is polled
    until every artifact is listed with the expected SHA256.
    """
    destination = Path(output_dir).resolve()

    if settings.mode == "disabled":
        if require_enabled:
            raise ValueError(
                "Debian publication is required for this run, but RELEASE_PUBLISH_MODE is disabled."
            )
        artifacts = _find_debian_publication_artifacts(destination)
        return DebianPublicationResult(
            output_dir=destination,
            mode=settings.mode,
            enabled=False,
            dry_run=dry_run,
            artifacts=artifacts,
            target_urls=(),
            skipped_reason="Publication mode is disabled.",
        )

    if settings.mode != "nexus":
        raise ValueError(f"Unsupported release publication mode: {settings.mode}")

    artifacts = select_debian_publication_artifacts(output_dir=destination)
    identities = tuple(identify_debian_artifact(path) for path in artifacts)
    verify_against_sha256sums(destination, identities, required=require_enabled and not dry_run)
    if lab_evidence is not None:
        verify_against_lab_evidence(Path(lab_evidence), identities)

    if index_loader is None:
        config = settings.apt_index_config()

        def load_index() -> AptIndex:
            return load_apt_index(config)

    else:
        load_index = index_loader

    plans = plan_debian_publication(identities, load_index(), allow_older_version=allow_older_version)
    for plan in plans:
        logger(f"[publish] {plan.artifact.path.name} sha256={plan.artifact.sha256}: {plan.action} ({plan.reason})")

    upload = _upload_file_to_nexus_with_curl if uploader is None else uploader
    target_url = settings.nexus_repo_url
    target_urls = tuple(target_url for _ in artifacts)
    verified = False
    if not dry_run:
        for plan in plans:
            if plan.action == ACTION_UPLOAD:
                upload(plan.artifact.path, target_url, settings.nexus_user, settings.nexus_password)
        verify_debian_publication(
            identities,
            index_loader=load_index,
            attempts=verify_attempts,
            delay_seconds=verify_delay_seconds,
            sleep=sleep,
            logger=logger,
        )
        verified = True

    return DebianPublicationResult(
        output_dir=destination,
        mode=settings.mode,
        enabled=True,
        dry_run=dry_run,
        artifacts=artifacts,
        target_urls=target_urls,
        skipped_reason=None,
        plans=plans,
        verified=verified,
    )


def _validate_nexus_repo_url(repo_url: str, *, name: str = "NEXUS_REPO_URL") -> None:
    parsed = urlparse(repo_url)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc:
        raise ValueError(
            f"{name} must be an absolute http(s) URL, "
            f"received `{repo_url}`."
        )


def _split_env_list(raw: str | None, *, default: tuple[str, ...]) -> tuple[str, ...]:
    if raw is None or not raw.strip():
        return default
    values = tuple(item.strip() for item in re.split(r"[,\s]+", raw) if item.strip())
    return values or default


def redact_url(url: str | None) -> str | None:
    if url is None:
        return None
    parsed = urlparse(url)
    if not parsed.username and not parsed.password:
        return url
    hostname = parsed.hostname or ""
    netloc = hostname
    if parsed.port is not None:
        netloc = f"{netloc}:{parsed.port}"
    if parsed.username or parsed.password:
        netloc = f"<redacted>@{netloc}"
    return urlunparse((parsed.scheme, netloc, parsed.path, parsed.params, parsed.query, parsed.fragment))


def _find_debian_publication_artifacts(destination: Path) -> tuple[Path, ...]:
    if not destination.is_dir():
        return ()
    return tuple(
        sorted(
            path
            for path in destination.iterdir()
            if path.is_file() and path.name.endswith(".deb")
        )
    )


def curl_config_for_credentials(username: str, password: str) -> str:
    """curl `--config -` text. Keeps credentials out of argv, which any local user can read via ps//proc."""

    def _quote(value: str) -> str:
        escaped = value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r")
        return f'"{escaped}"'

    return f"user = {_quote(f'{username}:{password}')}\n"


def _upload_file_to_nexus_with_curl(
    artifact: Path,
    target_url: str,
    username: str,
    password: str,
) -> None:
    try:
        completed = subprocess.run(
            (
                "curl",
                "--fail",
                "--silent",
                "--show-error",
                "--retry",
                "3",
                "--retry-all-errors",
                "--connect-timeout",
                "10",
                "--max-time",
                "300",
                "--config",
                "-",
                "-H",
                "Content-Type: multipart/form-data",
                "--data-binary",
                f"@{artifact}",
                target_url,
            ),
            input=curl_config_for_credentials(username, password),
            text=True,
            capture_output=True,
            check=False,
            timeout=1200,
        )
    except FileNotFoundError as exc:
        raise ValueError(
            "Debian publication failed: `curl` is not installed or not on PATH."
        ) from exc
    except Exception as exc:
        raise ValueError(
            f"Debian publication failed while uploading {artifact.name} to {target_url}: {exc}"
        ) from exc

    if completed.returncode != 0:
        stderr = _tail_lines(completed.stderr, limit=20)
        raise ValueError(
            "Debian publication failed: Nexus upload returned a non-zero exit code "
            f"for {artifact.name} -> {target_url} "
            f"(upload_mode=post-binary-to-base-url, append_filename=no, exit {completed.returncode}).\n{stderr}"
        )


def _tail_lines(content: str, *, limit: int) -> str:
    lines = [line for line in content.splitlines() if line.strip()]
    if not lines:
        return "(no stderr output)"
    return "\n".join(lines[-limit:])
