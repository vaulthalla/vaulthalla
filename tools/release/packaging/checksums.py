"""SHA256SUMS emission and verification for staged release artifacts.

The file uses the coreutils `sha256sum` format (`<hex>  <name>`), so operators can check a
downloaded GitHub release with `sha256sum -c SHA256SUMS --ignore-missing`.
"""

from __future__ import annotations

import hashlib
import re
from pathlib import Path

SHA256SUMS_NAME = "SHA256SUMS"
CHECKSUMMED_SUFFIXES: tuple[str, ...] = (
    ".deb",
    ".udeb",
    ".ddeb",
    ".changes",
    ".buildinfo",
    "_next-standalone.tar.gz",
)
_LINE_PATTERN = re.compile(r"^(?P<digest>[0-9a-f]{64}) [ *](?P<name>[^/\\]+)$")


def sha256_file(path: Path | str) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def checksummed_artifacts(output_dir: Path | str) -> tuple[Path, ...]:
    root = Path(output_dir)
    if not root.is_dir():
        return ()
    return tuple(
        sorted(
            path
            for path in root.iterdir()
            if path.is_file() and any(path.name.endswith(suffix) for suffix in CHECKSUMMED_SUFFIXES)
        )
    )


def render_sha256sums(entries: dict[str, str]) -> str:
    return "".join(f"{entries[name]}  {name}\n" for name in sorted(entries))


def write_sha256sums(output_dir: Path | str) -> Path:
    root = Path(output_dir)
    entries = {path.name: sha256_file(path) for path in checksummed_artifacts(root)}
    if not entries:
        raise ValueError(f"Cannot write {SHA256SUMS_NAME}: no checksummable release artifacts under {root}.")
    target = root / SHA256SUMS_NAME
    target.write_text(render_sha256sums(entries), encoding="utf-8")
    return target


def read_sha256sums(path: Path | str) -> dict[str, str]:
    source = Path(path)
    entries: dict[str, str] = {}
    for number, raw in enumerate(source.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line:
            continue
        match = _LINE_PATTERN.fullmatch(line)
        if not match:
            raise ValueError(f"{source}:{number}: malformed {SHA256SUMS_NAME} line: {raw!r}")
        name = match.group("name")
        if name in entries:
            raise ValueError(f"{source}:{number}: duplicate entry for {name}")
        entries[name] = match.group("digest")
    return entries


def verify_sha256sums(output_dir: Path | str) -> list[str]:
    """Return integrity issues; empty means every listed and every checksummable file matches."""
    root = Path(output_dir)
    sums_path = root / SHA256SUMS_NAME
    if not sums_path.is_file():
        return [f"{SHA256SUMS_NAME} is missing under {root}"]
    try:
        entries = read_sha256sums(sums_path)
    except ValueError as exc:
        return [str(exc)]

    issues: list[str] = []
    for name, expected in sorted(entries.items()):
        candidate = root / name
        if not candidate.is_file():
            issues.append(f"{SHA256SUMS_NAME} lists {name}, but the file is missing")
            continue
        actual = sha256_file(candidate)
        if actual != expected:
            issues.append(f"{name}: sha256 {actual} does not match {SHA256SUMS_NAME} entry {expected}")
    for artifact in checksummed_artifacts(root):
        if artifact.name not in entries:
            issues.append(f"{artifact.name} is not listed in {SHA256SUMS_NAME}")
    return issues
