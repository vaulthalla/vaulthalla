"""Contract: shipped deploy/psql migrations never change content without a reviewed allowlist entry.

SqlDeployer records sha256(raw bytes) per migration and refuses to start when a recorded hash differs from the
file on disk. Editing a shipped migration in place therefore bricks every upgraded install that recorded the old
hash (v1.6.0 did this to 060_acl.sql). core/seed/shipped_migrations.lock pins the current hash of every migration
plus the historical hashes SqlDeployer accepts (kHistoricalMigrationChecksums).

The lock/file/allowlist checks always run (CI checkouts are shallow). The per-tag history check runs whenever v*
tags are present locally (full clone, or `fetch-depth: 0` in CI) and skips otherwise.
"""

from __future__ import annotations

from collections import defaultdict
from pathlib import Path
import hashlib
import re
import subprocess
import unittest

REPO_ROOT = Path(__file__).resolve().parents[4]
PSQL_DIR = REPO_ROOT / "deploy" / "psql"
LOCK_PATH = REPO_ROOT / "core" / "seed" / "shipped_migrations.lock"
DEPLOYER_PATH = REPO_ROOT / "core" / "seed" / "include" / "SqlDeployer.hpp"

SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _load_lock() -> tuple[dict[str, str], dict[str, set[str]]]:
    current: dict[str, str] = {}
    historical: dict[str, set[str]] = defaultdict(set)
    for lineno, raw in enumerate(LOCK_PATH.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 3:
            raise AssertionError(f"{LOCK_PATH.name}:{lineno}: malformed line: {raw!r}")
        status, digest, filename = parts[0], parts[1], parts[2]
        if not SHA256_RE.match(digest):
            raise AssertionError(f"{LOCK_PATH.name}:{lineno}: bad sha256 {digest!r}")
        if status == "current":
            if filename in current:
                raise AssertionError(f"{LOCK_PATH.name}:{lineno}: duplicate current entry for {filename}")
            current[filename] = digest
        elif status == "historical":
            historical[filename].add(digest)
        else:
            raise AssertionError(f"{LOCK_PATH.name}:{lineno}: unknown status {status!r}")
    return current, dict(historical)


def _load_cpp_allowlist() -> set[tuple[str, str]]:
    source = DEPLOYER_PATH.read_text(encoding="utf-8")
    start = source.index("kHistoricalMigrationChecksums{")
    end = source.index("};", start)
    block = source[start:end]
    return set(re.findall(r'HistoricalMigrationChecksum\{\s*"([^"]+\.sql)",\s*"([0-9a-f]{64})"', block))


def _git(*args: str, input_bytes: bytes | None = None) -> bytes:
    return subprocess.run(
        ["git", "-C", str(REPO_ROOT), *args],
        input=input_bytes,
        capture_output=True,
        check=True,
    ).stdout


def _release_tags() -> list[str]:
    try:
        out = _git("tag", "--list", "v*").decode()
    except (OSError, subprocess.CalledProcessError):
        return []
    return [t for t in out.split() if t]


class MigrationChecksumContractTests(unittest.TestCase):
    def test_every_migration_has_a_current_lock_entry_matching_its_bytes(self) -> None:
        current, _ = _load_lock()
        on_disk = {p.name: _sha256(p.read_bytes()) for p in sorted(PSQL_DIR.glob("*.sql"))}
        self.assertTrue(on_disk, "no migrations found")

        missing = sorted(set(on_disk) - set(current))
        self.assertFalse(
            missing,
            f"new migrations need a 'current ... unreleased' line in {LOCK_PATH.relative_to(REPO_ROOT)}: {missing}",
        )

        removed = sorted(set(current) - set(on_disk))
        self.assertFalse(removed, f"migrations must never be removed or renamed once pinned: {removed}")

        changed = sorted(name for name, digest in on_disk.items() if current[name] != digest)
        self.assertFalse(
            changed,
            "migration content no longer matches core/seed/shipped_migrations.lock: "
            f"{changed}. If the file has shipped in a v* tag, revert it and add the next-numbered migration "
            "instead; editing a shipped migration makes every upgraded install fail at startup. Only an "
            "unreleased migration may have its 'current' hash updated.",
        )

    def test_migration_numbers_are_unique(self) -> None:
        prefixes: dict[str, list[str]] = defaultdict(list)
        for p in PSQL_DIR.glob("*.sql"):
            prefixes[p.name.split("_", 1)[0]].append(p.name)
        dupes = {k: v for k, v in prefixes.items() if len(v) > 1}
        self.assertFalse(dupes, f"duplicate migration numbers: {dupes}")

    def test_historical_lock_entries_match_sqldeployer_allowlist(self) -> None:
        current, historical = _load_lock()
        lock_pairs = {(name, digest) for name, digests in historical.items() for digest in digests}
        cpp_pairs = _load_cpp_allowlist()
        self.assertTrue(cpp_pairs, "could not parse kHistoricalMigrationChecksums from SqlDeployer.hpp")
        self.assertEqual(lock_pairs, cpp_pairs)

        for name, digest in lock_pairs:
            self.assertIn(name, current, f"historical entry for unknown migration {name}")
            self.assertNotEqual(current[name], digest, f"historical hash equals current hash for {name}")

    def test_released_tags_only_shipped_pinned_migration_hashes(self) -> None:
        tags = _release_tags()
        if not tags:
            self.skipTest("no v* tags available (shallow checkout); lock/allowlist checks still ran")

        current, historical = _load_lock()
        blob_to_paths: dict[str, set[tuple[str, str]]] = defaultdict(set)
        for tag in tags:
            try:
                listing = _git("ls-tree", "-r", tag, "--", "deploy/psql/").decode()
            except subprocess.CalledProcessError:
                continue
            for row in listing.splitlines():
                meta, path = row.split("\t", 1)
                _mode, kind, blob = meta.split()
                if kind != "blob" or not path.endswith(".sql"):
                    continue
                blob_to_paths[blob].add((tag, Path(path).name))

        if not blob_to_paths:
            self.skipTest("no tag contains deploy/psql")

        blob_hash: dict[str, str] = {}
        batch = _git("cat-file", "--batch", input_bytes="".join(f"{b}\n" for b in blob_to_paths).encode())
        offset = 0
        while offset < len(batch):
            header_end = batch.index(b"\n", offset)
            blob, _kind, size = batch[offset:header_end].decode().split()
            start = header_end + 1
            blob_hash[blob] = _sha256(batch[start:start + int(size)])
            offset = start + int(size) + 1

        problems: list[str] = []
        for blob, uses in blob_to_paths.items():
            digest = blob_hash[blob]
            for tag, name in sorted(uses):
                if name not in current:
                    problems.append(f"{tag}: shipped {name}, which no longer exists")
                elif digest != current[name] and digest not in historical.get(name, set()):
                    problems.append(f"{tag}: shipped {name} with sha256 {digest}, which is neither current nor "
                                    "an accepted historical checksum")
        self.assertFalse(
            problems,
            "released migrations changed in place without a reviewed allowlist entry:\n  " + "\n  ".join(problems),
        )


if __name__ == "__main__":
    unittest.main()
