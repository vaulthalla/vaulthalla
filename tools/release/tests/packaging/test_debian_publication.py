from __future__ import annotations

import gzip
import json
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from tools.release.packaging.apt_index import (
    AptIndex,
    AptIndexConfig,
    AptPackageEntry,
    compare_debian_versions,
    load_apt_index,
    parse_packages_index,
)
from tools.release.packaging.checksums import sha256_file, write_sha256sums
from tools.release.packaging.publication import (
    ACTION_SKIP_IDENTICAL,
    ACTION_UPLOAD,
    DebianPublicationSettings,
    PublicationIntegrityError,
    _upload_file_to_nexus_with_curl,
    curl_config_for_credentials,
    publish_debian_artifacts,
    redact_url,
    resolve_debian_publication_settings,
    select_debian_publication_artifacts,
)


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


class FakeAptRepo:
    """In-memory APT repository: uploads land in the index; the index is what publication reads."""

    def __init__(self, entries: list[AptPackageEntry] | None = None, *, visible_after_loads: int = 0) -> None:
        self.entries = list(entries or [])
        self.uploads: list[tuple[Path, str, str, str]] = []
        self.loads = 0
        self._pending: list[AptPackageEntry] = []
        self._visible_after_loads = visible_after_loads

    def uploader(self, artifact: Path, target_url: str, username: str, password: str) -> None:
        self.uploads.append((artifact, target_url, username, password))
        package, version, arch = artifact.name[: -len(".deb")].split("_")
        self._pending.append(AptPackageEntry(package, version, arch, sha256_file(artifact)))

    def load(self) -> AptIndex:
        self.loads += 1
        if self._pending and self.loads > self._visible_after_loads:
            self.entries.extend(self._pending)
            self._pending = []
        return AptIndex(entries=list(self.entries), sources=["fake"])


def _no_sleep(_seconds: float) -> None:
    return None


def _quiet(_line: str) -> None:
    return None


class DebianPublicationSettingsTests(unittest.TestCase):
    def test_settings_default_to_disabled(self) -> None:
        settings = resolve_debian_publication_settings(env={})
        self.assertEqual(settings.mode, "disabled")
        self.assertEqual(settings.nexus_repo_url, "")

    def test_settings_fail_when_mode_is_invalid(self) -> None:
        with self.assertRaisesRegex(ValueError, "Unsupported release publication mode"):
            _ = resolve_debian_publication_settings(mode="invalid", env={})

    def test_settings_fail_when_nexus_mode_missing_required_env(self) -> None:
        with self.assertRaisesRegex(ValueError, "NEXUS_REPO_URL, NEXUS_USER, NEXUS_PASS"):
            _ = resolve_debian_publication_settings(mode="nexus", env={})

    def test_settings_fail_when_required_publication_uses_nexus_without_credentials(self) -> None:
        with self.assertRaisesRegex(ValueError, "required Nexus configuration is missing"):
            _ = resolve_debian_publication_settings(
                mode="nexus",
                env={
                    "NEXUS_REPO_URL": "https://nexus.example/repository/vaulthalla-debian",
                    "NEXUS_USER": "",
                    "NEXUS_PASS": "",
                },
            )

    def test_settings_fail_when_nexus_url_is_not_absolute_http(self) -> None:
        with self.assertRaisesRegex(ValueError, "NEXUS_REPO_URL must be an absolute http"):
            _ = resolve_debian_publication_settings(
                mode="nexus",
                env={
                    "NEXUS_REPO_URL": "/relative/path",
                    "NEXUS_USER": "ci-user",
                    "NEXUS_PASS": "secret",
                },
            )

    def test_settings_prefer_apt_repository_url_for_index_reads(self) -> None:
        settings = resolve_debian_publication_settings(
            mode="nexus",
            env={
                "NEXUS_REPO_URL": "https://nexus.example/repository/vaulthalla-debian",
                "NEXUS_USER": "ci-user",
                "NEXUS_PASS": "secret",
                "RELEASE_APT_REPOSITORY_URL": "https://apt.example",
                "RELEASE_APT_SUITE": "stable",
                "RELEASE_APT_ARCHITECTURES": "amd64,arm64",
            },
        )
        config = settings.apt_index_config()
        self.assertEqual(config.repository_url, "https://apt.example")
        self.assertEqual(config.architectures, ("amd64", "arm64"))

    def test_redact_url_hides_embedded_credentials(self) -> None:
        self.assertEqual(
            redact_url("https://ci-user:secret@nexus.example/repository/vaulthalla"),
            "https://<redacted>@nexus.example/repository/vaulthalla",
        )


class DebianPublicationTests(unittest.TestCase):
    def _settings_disabled(self) -> DebianPublicationSettings:
        return DebianPublicationSettings(
            mode="disabled",
            nexus_repo_url="",
            nexus_user="",
            nexus_password="",
        )

    def _settings_nexus(self) -> DebianPublicationSettings:
        return DebianPublicationSettings(
            mode="nexus",
            nexus_repo_url="https://nexus.example/repository/vaulthalla-debian",
            nexus_user="ci-user",
            nexus_password="secret",
        )

    def _publish(self, output_dir: Path, repo: FakeAptRepo, **kwargs):
        kwargs.setdefault("settings", self._settings_nexus())
        return publish_debian_artifacts(
            output_dir=output_dir,
            uploader=repo.uploader,
            index_loader=repo.load,
            sleep=_no_sleep,
            logger=_quiet,
            **kwargs,
        )

    def test_select_debian_publication_artifacts_fails_when_no_deb_exists(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            output_dir.mkdir(parents=True, exist_ok=True)

            with self.assertRaisesRegex(ValueError, "no Debian package artifacts were found"):
                _ = select_debian_publication_artifacts(output_dir=output_dir)

    def test_select_debian_publication_artifacts_only_returns_deb_files(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            output_dir.mkdir(parents=True, exist_ok=True)
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.changes", "changes")
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.buildinfo", "buildinfo")

            artifacts = select_debian_publication_artifacts(output_dir=output_dir)
            self.assertEqual([path.name for path in artifacts], ["vaulthalla_1.2.3-1_amd64.deb"])

    def test_publish_returns_skipped_result_when_disabled(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            output_dir.mkdir(parents=True, exist_ok=True)
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")

            result = publish_debian_artifacts(
                output_dir=output_dir,
                settings=self._settings_disabled(),
            )

            self.assertFalse(result.enabled)
            self.assertEqual(result.mode, "disabled")
            self.assertIn("disabled", result.skipped_reason or "")
            self.assertEqual(len(result.artifacts), 1)
            self.assertEqual(result.target_urls, ())

    def test_publish_required_fails_when_mode_disabled(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            output_dir.mkdir(parents=True, exist_ok=True)
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")

            with self.assertRaisesRegex(ValueError, "publication is required.*RELEASE_PUBLISH_MODE is disabled"):
                _ = publish_debian_artifacts(
                    output_dir=output_dir,
                    settings=self._settings_disabled(),
                    require_enabled=True,
                )

    def test_publish_disabled_mode_skips_cleanly_when_output_dir_is_missing(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release-missing"
            result = publish_debian_artifacts(
                output_dir=output_dir,
                settings=self._settings_disabled(),
            )

            self.assertFalse(result.enabled)
            self.assertEqual(result.artifacts, ())
            self.assertEqual(result.target_urls, ())

    def test_publish_dry_run_validates_targets_without_upload(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            output_dir.mkdir(parents=True, exist_ok=True)
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")
            repo = FakeAptRepo()

            result = self._publish(output_dir, repo, dry_run=True)

            self.assertTrue(result.enabled)
            self.assertTrue(result.dry_run)
            self.assertEqual(repo.uploads, [])
            self.assertEqual(result.plans[0].action, ACTION_UPLOAD)
            self.assertEqual(
                result.target_urls,
                ("https://nexus.example/repository/vaulthalla-debian",),
            )

    def test_publish_uploads_all_selected_debs_in_sorted_order(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            output_dir.mkdir(parents=True, exist_ok=True)
            _write(output_dir / "vaulthalla_1.2.3-1_arm64.deb", "deb-arm")
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb-amd")
            repo = FakeAptRepo()

            result = self._publish(output_dir, repo, dry_run=False)

            self.assertTrue(result.enabled)
            self.assertFalse(result.dry_run)
            self.assertTrue(result.verified)
            self.assertEqual(len(result.artifacts), 2)
            self.assertEqual(
                [call[0].name for call in repo.uploads],
                [
                    "vaulthalla_1.2.3-1_amd64.deb",
                    "vaulthalla_1.2.3-1_arm64.deb",
                ],
            )
            self.assertEqual(
                [call[1] for call in repo.uploads],
                [
                    "https://nexus.example/repository/vaulthalla-debian",
                    "https://nexus.example/repository/vaulthalla-debian",
                ],
            )
            self.assertEqual([call[2] for call in repo.uploads], ["ci-user", "ci-user"])
            self.assertEqual([call[3] for call in repo.uploads], ["secret", "secret"])

    def test_publish_required_succeeds_with_valid_nexus_config(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            output_dir.mkdir(parents=True, exist_ok=True)
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")
            write_sha256sums(output_dir)
            repo = FakeAptRepo()

            result = self._publish(output_dir, repo, require_enabled=True)

            self.assertTrue(result.enabled)
            self.assertEqual(len(repo.uploads), 1)
            self.assertEqual(result.uploaded, (output_dir.resolve() / "vaulthalla_1.2.3-1_amd64.deb",))

    def test_publication_target_url_is_not_filename_appended(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            output_dir.mkdir(parents=True, exist_ok=True)
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")

            result = self._publish(output_dir, FakeAptRepo(), dry_run=True)

            self.assertEqual(len(result.target_urls), 1)
            self.assertEqual(result.target_urls[0], "https://nexus.example/repository/vaulthalla-debian")
            self.assertNotIn("vaulthalla_1.2.3-1_amd64.deb", result.target_urls[0])

    # --- idempotency + integrity -------------------------------------------------------------

    def test_rerun_with_identical_bytes_skips_upload_and_succeeds(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            deb = output_dir / "vaulthalla_1.2.3-1_amd64.deb"
            _write(deb, "deb")
            write_sha256sums(output_dir)
            repo = FakeAptRepo([AptPackageEntry("vaulthalla", "1.2.3-1", "amd64", sha256_file(deb))])

            result = self._publish(output_dir, repo, require_enabled=True)

            self.assertEqual(repo.uploads, [])
            self.assertEqual(result.plans[0].action, ACTION_SKIP_IDENTICAL)
            self.assertEqual(result.uploaded, ())
            self.assertTrue(result.verified)

    def test_rerun_with_different_bytes_refuses_to_overwrite(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "rebuilt-deb")
            write_sha256sums(output_dir)
            repo = FakeAptRepo([AptPackageEntry("vaulthalla", "1.2.3-1", "amd64", "0" * 64)])

            with self.assertRaisesRegex(PublicationIntegrityError, "REFUSING TO OVERWRITE A PUBLISHED VERSION"):
                _ = self._publish(output_dir, repo, require_enabled=True)
            self.assertEqual(repo.uploads, [])

    def test_published_entry_without_sha256_is_not_treated_as_identical(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")
            repo = FakeAptRepo([AptPackageEntry("vaulthalla", "1.2.3-1", "amd64", None)])

            with self.assertRaisesRegex(PublicationIntegrityError, "no SHA256"):
                _ = self._publish(output_dir, repo, dry_run=True)

    def test_older_version_than_newest_published_is_refused_by_default(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")
            write_sha256sums(output_dir)
            repo = FakeAptRepo([AptPackageEntry("vaulthalla", "1.10.0-1", "amd64", "a" * 64)])

            with self.assertRaisesRegex(PublicationIntegrityError, "newer version 1.10.0-1"):
                _ = self._publish(output_dir, repo, require_enabled=True)
            result = self._publish(output_dir, repo, require_enabled=True, allow_older_version=True)
            self.assertEqual(len(repo.uploads), 1)
            self.assertTrue(result.verified)

    def test_required_publication_needs_sha256sums_and_matching_digest(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            deb = output_dir / "vaulthalla_1.2.3-1_amd64.deb"
            _write(deb, "deb")
            with self.assertRaisesRegex(PublicationIntegrityError, "SHA256SUMS is missing"):
                _ = self._publish(output_dir, FakeAptRepo(), require_enabled=True)

            write_sha256sums(output_dir)
            _write(deb, "changed-after-validation")
            with self.assertRaisesRegex(PublicationIntegrityError, "does not match SHA256SUMS"):
                _ = self._publish(output_dir, FakeAptRepo(), require_enabled=True)

    def test_verification_polls_until_index_lists_expected_sha(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")
            write_sha256sums(output_dir)
            repo = FakeAptRepo(visible_after_loads=3)
            sleeps: list[float] = []

            result = publish_debian_artifacts(
                output_dir=output_dir,
                settings=self._settings_nexus(),
                uploader=repo.uploader,
                index_loader=repo.load,
                require_enabled=True,
                verify_attempts=5,
                verify_delay_seconds=2.0,
                sleep=sleeps.append,
                logger=_quiet,
            )

            self.assertTrue(result.verified)
            self.assertEqual(sleeps, [2.0, 2.0])

    def test_verification_fails_when_index_never_lists_the_version(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")
            write_sha256sums(output_dir)
            repo = FakeAptRepo(visible_after_loads=100)

            with self.assertRaisesRegex(ValueError, "did not confirm the release after 3 attempts"):
                _ = self._publish(output_dir, repo, require_enabled=True, verify_attempts=3)

    def test_verification_fails_loudly_when_index_lists_other_bytes(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")
            write_sha256sums(output_dir)
            repo = FakeAptRepo()

            def _corrupting_uploader(artifact: Path, *_args: str) -> None:
                repo.entries.append(AptPackageEntry("vaulthalla", "1.2.3-1", "amd64", "f" * 64))

            with self.assertRaisesRegex(PublicationIntegrityError, "expected"):
                _ = publish_debian_artifacts(
                    output_dir=output_dir,
                    settings=self._settings_nexus(),
                    uploader=_corrupting_uploader,
                    index_loader=repo.load,
                    require_enabled=True,
                    sleep=_no_sleep,
                    logger=_quiet,
                )

    def test_lab_evidence_must_have_passed_for_the_same_sha(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            deb = output_dir / "vaulthalla_1.2.3-1_amd64.deb"
            _write(deb, "deb")
            write_sha256sums(output_dir)
            evidence = Path(temp_dir) / "lab-smoke.json"

            def _evidence(ok: bool, sha: str) -> None:
                evidence.write_text(
                    json.dumps(
                        {"schema_version": "vaulthalla.release.lab_smoke.v1", "ok": ok, "candidate": {"sha256": sha}}
                    ),
                    encoding="utf-8",
                )

            _evidence(True, "0" * 64)
            with self.assertRaisesRegex(PublicationIntegrityError, "not the package the lab smoke tested"):
                _ = self._publish(output_dir, FakeAptRepo(), require_enabled=True, lab_evidence=evidence)

            _evidence(False, sha256_file(deb))
            with self.assertRaisesRegex(PublicationIntegrityError, "reports failure"):
                _ = self._publish(output_dir, FakeAptRepo(), require_enabled=True, lab_evidence=evidence)

            _evidence(True, sha256_file(deb))
            result = self._publish(output_dir, FakeAptRepo(), require_enabled=True, lab_evidence=evidence)
            self.assertTrue(result.verified)

    # --- curl transport ----------------------------------------------------------------------

    def test_curl_upload_uses_post_binary_base_url_shape(self) -> None:
        artifact = Path("/tmp/vaulthalla_1.2.3-1_amd64.deb")
        target_url = "https://nexus.example/repository/vaulthalla-debian"

        with patch(
            "tools.release.packaging.publication.subprocess.run",
            return_value=subprocess.CompletedProcess(args=("curl",), returncode=0, stdout="", stderr=""),
        ) as run:
            _upload_file_to_nexus_with_curl(artifact, target_url, "ci-user", "secret")

        run.assert_called_once()
        command = run.call_args.args[0]
        self.assertIn("--data-binary", command)
        self.assertIn(f"@{artifact}", command)
        self.assertIn("Content-Type: multipart/form-data", command)
        self.assertNotIn("--upload-file", command)
        self.assertEqual(command[-1], target_url)

    def test_curl_upload_keeps_credentials_out_of_argv(self) -> None:
        artifact = Path("/tmp/vaulthalla_1.2.3-1_amd64.deb")
        with patch(
            "tools.release.packaging.publication.subprocess.run",
            return_value=subprocess.CompletedProcess(args=("curl",), returncode=0, stdout="", stderr=""),
        ) as run:
            _upload_file_to_nexus_with_curl(artifact, "https://nexus.example/r", "ci-user", "s3cr3t")

        command = run.call_args.args[0]
        self.assertNotIn("--user", command)
        self.assertFalse(any("s3cr3t" in part for part in command))
        self.assertIn("--config", command)
        self.assertEqual(command[command.index("--config") + 1], "-")
        self.assertEqual(run.call_args.kwargs["input"], 'user = "ci-user:s3cr3t"\n')

    def test_curl_config_escapes_quotes_and_backslashes(self) -> None:
        self.assertEqual(curl_config_for_credentials("u", 'p"a\\ss'), 'user = "u:p\\"a\\\\ss"\n')

    def test_curl_upload_failure_reports_upload_mode_and_no_append(self) -> None:
        artifact = Path("/tmp/vaulthalla_1.2.3-1_amd64.deb")
        target_url = "https://nexus.example/repository/vaulthalla-debian"

        with (
            patch(
                "tools.release.packaging.publication.subprocess.run",
                return_value=subprocess.CompletedProcess(args=("curl",), returncode=22, stdout="", stderr="HTTP 405"),
            ),
            self.assertRaisesRegex(ValueError, "upload_mode=post-binary-to-base-url, append_filename=no"),
        ):
            _upload_file_to_nexus_with_curl(artifact, target_url, "ci-user", "secret")


class AptIndexTests(unittest.TestCase):
    PACKAGES = (
        "Package: vaulthalla\n"
        "Version: 1.6.5-1\n"
        "Architecture: amd64\n"
        "Filename: pool/v/vaulthalla/vaulthalla_1.6.5-1_amd64.deb\n"
        "Size: 1234\n"
        "SHA256: " + "A" * 64 + "\n"
        "Description: vault\n"
        " continued\n"
        "\n"
        "Package: vaulthalla\n"
        "Version: 1.6.6-1\n"
        "Architecture: amd64\n"
        "SHA256: " + "b" * 64 + "\n"
    )

    def test_parse_packages_index_keeps_sha256_lowercased(self) -> None:
        entries = parse_packages_index(self.PACKAGES)
        self.assertEqual([entry.version for entry in entries], ["1.6.5-1", "1.6.6-1"])
        self.assertEqual(entries[0].sha256, "a" * 64)
        self.assertEqual(entries[0].size, 1234)
        index = AptIndex(entries=entries)
        self.assertEqual(index.newest_version("vaulthalla"), "1.6.6-1")
        self.assertEqual(len(index.lookup("vaulthalla", "1.6.6-1", "amd64")), 1)
        self.assertEqual(index.lookup("vaulthalla", "1.6.6-1", "arm64"), [])

    def test_load_apt_index_prefers_gz_and_fails_closed_when_unreadable(self) -> None:
        calls: list[str] = []

        def _get(url: str, headers):
            calls.append(url)
            if url.endswith(".gz"):
                return gzip.compress(self.PACKAGES.encode("utf-8"))
            raise ValueError("HTTP 404")

        index = load_apt_index(AptIndexConfig(repository_url="https://apt.example/"), http_get=_get)
        self.assertEqual(calls, ["https://apt.example/dists/stable/main/binary-amd64/Packages.gz"])
        self.assertEqual(len(index.entries), 2)

        def _down(url: str, headers):
            raise ValueError("connection refused")

        with self.assertRaisesRegex(ValueError, "refusing to guess"):
            _ = load_apt_index(AptIndexConfig(repository_url="https://apt.example"), http_get=_down)

    def test_compare_debian_versions_matches_dpkg_ordering(self) -> None:
        cases = [
            ("1.6.10-1", "1.6.9-1", 1),
            ("1.6.6-1", "1.6.6-1", 0),
            ("1.6.6-2", "1.6.6-1", 1),
            ("1.0~rc1-1", "1.0-1", -1),
            ("1:0.1-1", "9.9-1", 1),
            ("1.6.6", "1.6.6-0", 0),
            ("1.6.6a-1", "1.6.6-1", 1),
        ]
        for left, right, expected in cases:
            with self.subTest(left=left, right=right):
                result = compare_debian_versions(left, right)
                self.assertEqual((result > 0) - (result < 0), expected)


if __name__ == "__main__":
    unittest.main()
