from __future__ import annotations

import gzip
import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from tools.release.shipped_release import (
    record_release_success,
    resolve_release_notes_base,
)
from tools.release.changelog.context_builder import build_release_context


def _git(repo: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=repo,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(f"git {' '.join(args)} failed: {result.stderr}")
    return result.stdout


def _init_repo(root: Path) -> Path:
    _git(root, "init", "-q")
    _git(root, "config", "user.email", "release-tests@example.com")
    _git(root, "config", "user.name", "Release Tests")
    _write(root / "debian" / "control", "Source: vaulthalla\n\nPackage: vaulthalla\nArchitecture: any\n")
    _commit(root, "initial")
    return root


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def _commit(repo: Path, message: str) -> None:
    _git(repo, "add", ".")
    _git(repo, "commit", "-q", "-m", message)


def _tagged_release(repo: Path, version: str) -> None:
    _write(repo / "VERSION", f"{version}\n")
    _write(repo / "marker.txt", f"{version}\n")
    _commit(repo, f"release {version}")
    _git(repo, "tag", f"v{version}")


def _packages_index(*versions: str, package: str = "vaulthalla") -> bytes:
    stanzas = [f"Package: {package}\nVersion: {version}-1\nArchitecture: amd64\n" for version in versions]
    return "\n".join(stanzas).encode("utf-8")


class ShippedReleaseResolverTests(unittest.TestCase):
    def _repo_with_1_6_0(self) -> Path:
        temp_dir = TemporaryDirectory()
        self.addCleanup(temp_dir.cleanup)
        repo = _init_repo(Path(temp_dir.name))
        _tagged_release(repo, "1.5.1")
        _tagged_release(repo, "1.6.0")
        _write(repo / "VERSION", "1.6.1\n")
        _write(repo / "patch.txt", "1.6.1\n")
        _commit(repo, "release 1.6.1")
        return repo

    def _repo_with_failed_1_6_0_and_1_6_1(self) -> Path:
        temp_dir = TemporaryDirectory()
        self.addCleanup(temp_dir.cleanup)
        repo = _init_repo(Path(temp_dir.name))
        _tagged_release(repo, "1.5.1")
        _tagged_release(repo, "1.6.0")
        _tagged_release(repo, "1.6.1")
        _write(repo / "VERSION", "1.6.2\n")
        _write(repo / "patch.txt", "1.6.2\n")
        _commit(repo, "release 1.6.2")
        return repo

    def test_failed_tagged_release_skips_to_last_published_apt_version(self) -> None:
        repo = self._repo_with_1_6_0()

        def fake_get(_url, _headers):
            return gzip.compress(_packages_index("1.5.1"))

        result = resolve_release_notes_base(
            repo_root=repo,
            version="1.6.1",
            env={
                "RELEASE_PUBLISH_MODE": "nexus",
                "RELEASE_APT_REPOSITORY_URL": "https://apt.example/repository/vaulthalla",
            },
            http_get=fake_get,
        )

        self.assertEqual(result.last_successful_release_tag, "v1.5.1")
        self.assertEqual(result.source, "nexus-apt-metadata")
        self.assertEqual(result.commit_range, "v1.5.1..HEAD")
        self.assertEqual([item.tag for item in result.skipped_release_tags], ["v1.6.0"])

        context = build_release_context(
            version="1.6.1",
            repo_root=repo,
            previous_tag=result.last_successful_release_tag,
        )
        subjects = [commit.subject for commit in context.commits]
        self.assertIn("release 1.6.0", subjects)
        self.assertIn("release 1.6.1", subjects)

    def test_recovery_after_1_6_1_failure_uses_1_5_1_for_1_6_2(self) -> None:
        repo = self._repo_with_failed_1_6_0_and_1_6_1()

        def fake_get(_url, _headers):
            return gzip.compress(_packages_index("1.5.1"))

        result = resolve_release_notes_base(
            repo_root=repo,
            version="1.6.2",
            env={
                "RELEASE_PUBLISH_MODE": "nexus",
                "RELEASE_APT_REPOSITORY_URL": "https://apt.example/repository/vaulthalla",
            },
            http_get=fake_get,
        )

        self.assertEqual(result.last_successful_release_tag, "v1.5.1")
        self.assertEqual(result.source, "nexus-apt-metadata")
        self.assertEqual(result.commit_range, "v1.5.1..HEAD")
        self.assertEqual([item.tag for item in result.skipped_release_tags], ["v1.6.1", "v1.6.0"])

        context = build_release_context(
            version="1.6.2",
            repo_root=repo,
            previous_tag=result.last_successful_release_tag,
        )
        subjects = [commit.subject for commit in context.commits]
        self.assertIn("release 1.6.0", subjects)
        self.assertIn("release 1.6.1", subjects)
        self.assertIn("release 1.6.2", subjects)

    def test_successful_apt_publication_uses_nearest_lower_tag(self) -> None:
        repo = self._repo_with_1_6_0()

        def fake_get(_url, _headers):
            return gzip.compress(_packages_index("1.5.1", "1.6.0"))

        result = resolve_release_notes_base(
            repo_root=repo,
            version="1.6.1",
            env={
                "RELEASE_PUBLISH_MODE": "nexus",
                "RELEASE_APT_REPOSITORY_URL": "https://apt.example/repository/vaulthalla",
            },
            http_get=fake_get,
        )

        self.assertEqual(result.last_successful_release_tag, "v1.6.0")
        self.assertEqual(result.skipped_release_tags, ())

    def test_missing_apt_package_does_not_count_as_successful(self) -> None:
        repo = self._repo_with_1_6_0()

        def fake_get(_url, _headers):
            return gzip.compress(_packages_index("1.5.1"))

        result = resolve_release_notes_base(
            repo_root=repo,
            version="1.6.1",
            env={
                "RELEASE_PUBLISH_MODE": "nexus",
                "RELEASE_APT_REPOSITORY_URL": "https://apt.example/repository/vaulthalla",
            },
            http_get=fake_get,
        )

        self.assertEqual(result.last_successful_release_tag, "v1.5.1")
        self.assertIn("missing expected package set", result.skipped_release_tags[0].reason)

    def test_partial_package_set_does_not_count_as_successful(self) -> None:
        repo = self._repo_with_1_6_0()

        def fake_get(_url, _headers):
            return gzip.compress(_packages_index("1.6.0", package="vaulthalla"))

        with self.assertRaisesRegex(ValueError, "Cannot determine shipped release baseline"):
            _ = resolve_release_notes_base(
                repo_root=repo,
                version="1.6.1",
                env={
                    "RELEASE_PUBLISH_MODE": "nexus",
                    "RELEASE_APT_REPOSITORY_URL": "https://apt.example/repository/vaulthalla",
                    "RELEASE_EXPECTED_DEBIAN_PACKAGES": "vaulthalla vaulthalla-extra",
                },
                http_get=fake_get,
            )

    def test_github_release_does_not_override_missing_apt_in_nexus_mode(self) -> None:
        repo = self._repo_with_1_6_0()

        def fake_get(url, _headers):
            if "api.github.com" in url:
                return b'{"draft": false, "prerelease": false, "assets": [{"name": "x"}]}'
            return gzip.compress(_packages_index("1.5.1"))

        result = resolve_release_notes_base(
            repo_root=repo,
            version="1.6.1",
            env={
                "RELEASE_PUBLISH_MODE": "nexus",
                "RELEASE_APT_REPOSITORY_URL": "https://apt.example/repository/vaulthalla",
                "GITHUB_TOKEN": "token",
                "GITHUB_REPOSITORY": "vaulthalla/vaulthalla",
            },
            http_get=fake_get,
        )

        self.assertEqual(result.last_successful_release_tag, "v1.5.1")
        self.assertEqual(result.source, "nexus-apt-metadata")

    def test_operator_override_forces_base(self) -> None:
        repo = self._repo_with_1_6_0()

        result = resolve_release_notes_base(
            repo_root=repo,
            version="1.6.1",
            override_tag="v1.5.1",
            env={},
            http_get=lambda _url, _headers: b"",
        )

        self.assertEqual(result.last_successful_release_tag, "v1.5.1")
        self.assertEqual(result.source, "operator-override")
        self.assertEqual([item.tag for item in result.skipped_release_tags], ["v1.6.0"])

    def test_record_release_success_writes_ledger(self) -> None:
        repo = self._repo_with_1_6_0()
        release_dir = repo / "release"
        _write(release_dir / "vaulthalla_1.6.1-1_amd64.deb", "deb")

        ledger = record_release_success(
            repo_root=repo,
            output_dir=release_dir,
            tag="v1.6.1",
            version="1.6.1",
            publication_mode="nexus",
            target_url="https://user:secret@apt.example/repository/vaulthalla",
            workflow_run_id="123",
        )

        content = ledger.read_text(encoding="utf-8")
        self.assertIn("v1.6.1", content)
        self.assertIn("<redacted>@apt.example", content)
        self.assertNotIn("secret", content)


if __name__ == "__main__":
    unittest.main()
