from __future__ import annotations

import shutil
import subprocess
import tarfile
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from tools.release.version.adapters.debian import CHANGELOG_HEADER_PATTERN
from tools.release.version.validate import ReleasePaths, require_synced_release_state

DEBIAN_BUILD_COMMAND: tuple[str, ...] = ("dpkg-buildpackage", "-us", "-uc", "-b")
DEBIAN_REQUIRED_FILES: tuple[str, ...] = (
    "debian/changelog",
    "debian/control",
    "debian/rules",
    "debian/source/format",
)
WEB_REQUIRED_FILES: tuple[str, ...] = ("web/package.json",)
DEFAULT_OUTPUT_DIR_NAME = "release"
SUPPORTED_ARTIFACT_SUFFIXES: tuple[str, ...] = (
    ".deb",
    ".udeb",
    ".changes",
    ".buildinfo",
    ".dsc",
    ".build",
    ".tar.xz",
    ".tar.gz",
    ".debian.tar.xz",
    ".debian.tar.gz",
)
WEB_DEPLOYABLE_SUFFIX = "_next-standalone.tar.gz"
WEB_INSTALL_COMMAND: tuple[str, ...] = ("pnpm", "install", "--frozen-lockfile")
WEB_BUILD_COMMAND: tuple[str, ...] = ("pnpm", "build")


@dataclass(frozen=True)
class DebianBuildResult:
    repo_root: Path
    output_dir: Path
    command: tuple[str, ...]
    dry_run: bool
    package_name: str
    package_version: str
    artifacts: tuple[Path, ...]
    build_log: Path | None = None
    web_artifact: Path | None = None


def build_debian_package(
    *,
    repo_root: Path | str = ".",
    output_dir: Path | str | None = None,
    dry_run: bool = False,
) -> DebianBuildResult:
    root = Path(repo_root).resolve()
    state = require_synced_release_state(root)
    _require_debian_prerequisites(state.paths)
    _require_web_prerequisites(root)

    changelog_path = state.paths.debian_changelog_file
    package_name, package_version = _read_debian_changelog_identity(changelog_path)
    destination = _resolve_output_dir(root, output_dir)

    command = DEBIAN_BUILD_COMMAND
    if dry_run:
        return DebianBuildResult(
            repo_root=root,
            output_dir=destination,
            command=command,
            dry_run=True,
            package_name=package_name,
            package_version=package_version,
            artifacts=(),
            build_log=None,
            web_artifact=None,
        )

    _require_build_tool(command[0])
    _require_build_tool("pnpm")
    _ensure_output_dir_writable(destination)

    web_install = _run_command(command=WEB_INSTALL_COMMAND, cwd=root / "web")
    if web_install.returncode != 0:
        log = _write_build_log(
            destination=destination,
            command=command,
            cwd=root,
            web_install=web_install,
            web_build=None,
            debian_build=None,
        )
        raise ValueError(
            f"Web dependency install failed with exit code {web_install.returncode}. "
            f"See log: {log}\n{_tail_lines(web_install.stderr, limit=25)}"
        )

    web_build = _run_command(command=WEB_BUILD_COMMAND, cwd=root / "web")
    if web_build.returncode != 0:
        log = _write_build_log(
            destination=destination,
            command=command,
            cwd=root,
            web_install=web_install,
            web_build=web_build,
            debian_build=None,
        )
        raise ValueError(
            f"Web build failed with exit code {web_build.returncode}. "
            f"See log: {log}\n{_tail_lines(web_build.stderr, limit=25)}"
        )

    web_artifact = _create_web_deployable_archive(
        repo_root=root,
        output_dir=destination,
        package_name=package_name,
        package_version=package_version,
    )

    completed = _run_command(command=command, cwd=root)
    build_log = _write_build_log(
        destination=destination,
        command=command,
        cwd=root,
        web_install=web_install,
        web_build=web_build,
        debian_build=completed,
    )

    if completed.returncode != 0:
        stderr_tail = _tail_lines(completed.stderr, limit=25)
        raise ValueError(
            f"Debian build failed with exit code {completed.returncode}. "
            f"See log: {build_log}\n{stderr_tail}"
        )

    artifacts = _collect_build_artifacts(
        search_dir=root.parent,
        output_dir=destination,
        package_name=package_name,
        package_version=package_version,
    )
    if not artifacts:
        raise ValueError(
            "Debian build succeeded but no packaging artifacts were found. "
            f"Searched under {root.parent} for {package_name}_{package_version}*"
        )

    artifacts_all = tuple(sorted(set(artifacts + (web_artifact,))))
    return DebianBuildResult(
        repo_root=root,
        output_dir=destination,
        command=command,
        dry_run=False,
        package_name=package_name,
        package_version=package_version,
        artifacts=artifacts_all,
        build_log=build_log,
        web_artifact=web_artifact,
    )


def _require_debian_prerequisites(paths: ReleasePaths) -> None:
    missing: list[Path] = []
    for relative in DEBIAN_REQUIRED_FILES:
        path = paths.repo_root / relative
        if not path.is_file():
            missing.append(path)

    if missing:
        rendered = "\n".join(f"- {path}" for path in missing)
        raise ValueError(f"Debian packaging prerequisites are missing:\n{rendered}")


def _require_web_prerequisites(repo_root: Path) -> None:
    missing: list[Path] = []
    for relative in WEB_REQUIRED_FILES:
        path = repo_root / relative
        if not path.is_file():
            missing.append(path)
    if missing:
        rendered = "\n".join(f"- {path}" for path in missing)
        raise ValueError(f"Web packaging prerequisites are missing:\n{rendered}")


def _read_debian_changelog_identity(changelog_path: Path) -> tuple[str, str]:
    lines = changelog_path.read_text(encoding="utf-8").splitlines()
    if not lines:
        raise ValueError(f"Debian changelog is empty: {changelog_path}")
    first_line = lines[0].strip()
    match = CHANGELOG_HEADER_PATTERN.fullmatch(first_line)
    if not match:
        raise ValueError(f"Could not parse Debian changelog header in {changelog_path}: {first_line}")
    return match.group("package"), match.group("full_version")


def _resolve_output_dir(repo_root: Path, output_dir: Path | str | None) -> Path:
    if output_dir is None:
        return repo_root / DEFAULT_OUTPUT_DIR_NAME

    candidate = Path(output_dir)
    if not candidate.is_absolute():
        return (repo_root / candidate).resolve()
    return candidate


def _require_build_tool(tool_name: str) -> None:
    if shutil.which(tool_name) is None:
        raise ValueError(
            f"Required build tool `{tool_name}` is not installed or not on PATH."
        )


def _ensure_output_dir_writable(output_dir: Path) -> None:
    try:
        output_dir.mkdir(parents=True, exist_ok=True)
    except Exception as exc:
        raise ValueError(f"Failed to create output directory {output_dir}: {exc}") from exc

    probe = output_dir / ".write-probe"
    try:
        probe.write_text("ok", encoding="utf-8")
        probe.unlink(missing_ok=True)
    except Exception as exc:
        raise ValueError(f"Output directory is not writable: {output_dir} ({exc})") from exc


def _run_command(*, command: Iterable[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            tuple(command),
            cwd=str(cwd),
            text=True,
            capture_output=True,
            check=False,
        )
    except FileNotFoundError as exc:
        raise ValueError(f"Build command not found: {command}") from exc
    except Exception as exc:
        raise ValueError(f"Failed to execute command `{' '.join(command)}`: {exc}") from exc


def _write_build_log(
    destination: Path,
    command: Iterable[str],
    cwd: Path,
    web_install: subprocess.CompletedProcess[str] | None,
    web_build: subprocess.CompletedProcess[str] | None,
    debian_build: subprocess.CompletedProcess[str] | None,
) -> Path:
    log_path = destination / "build-deb.log"
    sections: list[str] = [f"debian_command: {' '.join(command)}", f"cwd: {cwd}", ""]
    sections.extend(_render_completed_process("WEB INSTALL", web_install))
    sections.extend(_render_completed_process("WEB BUILD", web_build))
    sections.extend(_render_completed_process("DEBIAN BUILD", debian_build))
    log_body = "\n".join(sections)
    log_path.write_text(log_body, encoding="utf-8")
    return log_path


def _collect_build_artifacts(
    *,
    search_dir: Path,
    output_dir: Path,
    package_name: str,
    package_version: str,
) -> tuple[Path, ...]:
    copied: list[Path] = []
    for path in sorted(search_dir.glob(f"{package_name}_{package_version}*")):
        if not path.is_file():
            continue
        if not _is_supported_artifact(path):
            continue

        destination = output_dir / path.name
        if path.resolve() != destination.resolve():
            shutil.copy2(path, destination)
        copied.append(destination)

    return tuple(copied)


def _create_web_deployable_archive(
    *,
    repo_root: Path,
    output_dir: Path,
    package_name: str,
    package_version: str,
) -> Path:
    web_root = repo_root / "web"
    standalone_dir = web_root / ".next" / "standalone"
    static_dir = web_root / ".next" / "static"
    public_dir = web_root / "public"

    if not standalone_dir.is_dir():
        raise ValueError(
            f"Next.js standalone output is missing: {standalone_dir}. "
            "Ensure `pnpm build` completed successfully."
        )
    if not static_dir.is_dir():
        raise ValueError(
            f"Next.js static output is missing: {static_dir}. "
            "Ensure `pnpm build` completed successfully."
        )

    archive_name = f"{package_name}-web_{package_version}{WEB_DEPLOYABLE_SUFFIX}"
    archive_path = output_dir / archive_name

    with tempfile.TemporaryDirectory(prefix="vh-web-artifact-") as temp_dir:
        staging_root = Path(temp_dir) / "vaulthalla-web"
        shutil.copytree(standalone_dir, staging_root, dirs_exist_ok=True)
        (staging_root / ".next").mkdir(parents=True, exist_ok=True)
        shutil.copytree(static_dir, staging_root / ".next" / "static", dirs_exist_ok=True)
        if public_dir.is_dir():
            shutil.copytree(public_dir, staging_root / "public", dirs_exist_ok=True)

        with tarfile.open(archive_path, "w:gz") as tar:
            tar.add(staging_root, arcname="vaulthalla-web")

    return archive_path


def _is_supported_artifact(path: Path) -> bool:
    name = path.name
    return any(name.endswith(suffix) for suffix in SUPPORTED_ARTIFACT_SUFFIXES)


def _tail_lines(content: str, *, limit: int) -> str:
    lines = [line for line in content.splitlines() if line.strip()]
    if not lines:
        return "(no stderr output)"
    return "\n".join(lines[-limit:])


def _render_completed_process(
    name: str,
    completed: subprocess.CompletedProcess[str] | None,
) -> list[str]:
    if completed is None:
        return [f"=== {name} ===", "skipped", ""]
    return [
        f"=== {name} ===",
        f"command: {' '.join(map(str, completed.args))}",
        f"exit_code: {completed.returncode}",
        "--- STDOUT ---",
        completed.stdout.rstrip(),
        "--- STDERR ---",
        completed.stderr.rstrip(),
        "",
    ]
