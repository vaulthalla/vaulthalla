from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from tools.release.packaging.debian import validate_release_artifacts


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


class ReleaseArtifactValidationTests(unittest.TestCase):
    def _valid_debian_members(self) -> set[str]:
        return {
            "usr/bin/vaulthalla-server",
            "usr/bin/vaulthalla-cli",
            "usr/bin/vaulthalla",
            "usr/bin/vh",
            "usr/share/vaulthalla/config/config.yaml",
            "usr/share/vaulthalla/config/config_template.yaml.in",
            "lib/systemd/system/vaulthalla.service",
            "lib/systemd/system/vaulthalla-web.service",
            "lib/systemd/system/vaulthalla-swtpm.service",
            "usr/share/doc/vaulthalla/LICENSE.gz",
            "usr/share/doc/vaulthalla/copyright",
            "usr/share/vaulthalla/nginx/vaulthalla",
            "usr/share/vaulthalla/psql/000_schema.sql",
            "usr/share/vaulthalla-web/server.js",
            "usr/share/vaulthalla-web/.next/static/chunks/main.js",
            "usr/share/man/man1/vh.1.gz",
            "usr/lib/x86_64-linux-gnu/udev/rules.d/60-vaulthalla-tpm.rules",
            "usr/lib/x86_64-linux-gnu/tmpfiles.d/vaulthalla.conf",
        }

    def _write_valid_web_archive(self, path: Path) -> None:
        import tarfile

        with tarfile.open(path, "w:gz") as tar:
            from io import BytesIO

            root = tarfile.TarInfo("vaulthalla-web/")
            root.type = tarfile.DIRTYPE
            tar.addfile(root)

            server = tarfile.TarInfo("vaulthalla-web/server.js")
            payload = b"console.log('ok')\n"
            server.size = len(payload)
            tar.addfile(server, BytesIO(payload))

            static_dir = tarfile.TarInfo("vaulthalla-web/.next/static/")
            static_dir.type = tarfile.DIRTYPE
            tar.addfile(static_dir)

            static = tarfile.TarInfo("vaulthalla-web/.next/static/chunks/main.js")
            static_payload = b"chunk\n"
            static.size = len(static_payload)
            tar.addfile(static, BytesIO(static_payload))

    def test_validation_passes_when_expected_artifacts_exist(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            output_dir.mkdir(parents=True, exist_ok=True)
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")
            self._write_valid_web_archive(output_dir / "vaulthalla-web_1.2.3-1_next-standalone.tar.gz")
            _write(output_dir / "changelog.release.md", "# release")
            _write(output_dir / "changelog.raw.md", "# raw")
            _write(output_dir / "changelog.payload.json", '{"schema_version":"x"}')
            _write(output_dir / "changelog.semantic_payload.json", '{"schema_version":"semantic"}')
            _write(output_dir / "changelog.context.json", '{"schema_version":"context"}')

            with patch(
                "tools.release.packaging.debian._read_debian_package_members",
                return_value=self._valid_debian_members(),
            ):
                result = validate_release_artifacts(output_dir=output_dir, require_changelog=True)

            self.assertEqual(result.output_dir, output_dir.resolve())
            self.assertEqual(len(result.debian_artifacts), 1)
            self.assertEqual(len(result.web_artifacts), 1)
            self.assertEqual(len(result.changelog_artifacts), 5)

    def test_validation_reports_missing_outputs_clearly(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            output_dir.mkdir(parents=True, exist_ok=True)
            _write(output_dir / "changelog.raw.md", "# raw")

            with self.assertRaisesRegex(ValueError, "Missing expected outputs"):
                _ = validate_release_artifacts(output_dir=output_dir, require_changelog=True)

    def test_validation_can_skip_changelog_checks(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            output_dir.mkdir(parents=True, exist_ok=True)
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")
            self._write_valid_web_archive(output_dir / "vaulthalla-web_1.2.3-1_next-standalone.tar.gz")

            with patch(
                "tools.release.packaging.debian._read_debian_package_members",
                return_value=self._valid_debian_members(),
            ):
                result = validate_release_artifacts(output_dir=output_dir, require_changelog=False)
            self.assertEqual(len(result.debian_artifacts), 1)
            self.assertEqual(len(result.web_artifacts), 1)

    def test_validation_fails_when_debian_package_missing_required_payload(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            output_dir.mkdir(parents=True, exist_ok=True)
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")
            self._write_valid_web_archive(output_dir / "vaulthalla-web_1.2.3-1_next-standalone.tar.gz")
            _write(output_dir / "changelog.release.md", "# release")
            _write(output_dir / "changelog.raw.md", "# raw")
            _write(output_dir / "changelog.payload.json", '{"schema_version":"x"}')
            _write(output_dir / "changelog.semantic_payload.json", '{"schema_version":"semantic"}')
            _write(output_dir / "changelog.context.json", '{"schema_version":"context"}')

            members = self._valid_debian_members()
            members.remove("usr/bin/vaulthalla-cli")
            with (
                patch("tools.release.packaging.debian._read_debian_package_members", return_value=members),
                self.assertRaisesRegex(ValueError, r"\[debian package\].*vaulthalla-cli"),
            ):
                _ = validate_release_artifacts(output_dir=output_dir, require_changelog=True)

    def test_validation_fails_when_web_archive_missing_runtime_entry(self) -> None:
        import tarfile
        from io import BytesIO

        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            output_dir.mkdir(parents=True, exist_ok=True)
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")
            bad_archive = output_dir / "vaulthalla-web_1.2.3-1_next-standalone.tar.gz"
            with tarfile.open(bad_archive, "w:gz") as tar:
                root = tarfile.TarInfo("vaulthalla-web/")
                root.type = tarfile.DIRTYPE
                tar.addfile(root)
                static = tarfile.TarInfo("vaulthalla-web/.next/static/chunks/main.js")
                payload = b"chunk\n"
                static.size = len(payload)
                tar.addfile(static, BytesIO(payload))
            _write(output_dir / "changelog.release.md", "# release")
            _write(output_dir / "changelog.raw.md", "# raw")
            _write(output_dir / "changelog.payload.json", '{"schema_version":"x"}')
            _write(output_dir / "changelog.semantic_payload.json", '{"schema_version":"semantic"}')
            _write(output_dir / "changelog.context.json", '{"schema_version":"context"}')

            with (
                patch(
                    "tools.release.packaging.debian._read_debian_package_members",
                    return_value=self._valid_debian_members(),
                ),
                self.assertRaisesRegex(ValueError, r"\[web artifact\].*server\.js"),
            ):
                _ = validate_release_artifacts(output_dir=output_dir, require_changelog=True)

    def test_validation_accepts_uncompressed_debian_license_doc_too(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            output_dir.mkdir(parents=True, exist_ok=True)
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")
            self._write_valid_web_archive(output_dir / "vaulthalla-web_1.2.3-1_next-standalone.tar.gz")
            _write(output_dir / "changelog.release.md", "# release")
            _write(output_dir / "changelog.raw.md", "# raw")
            _write(output_dir / "changelog.payload.json", '{"schema_version":"x"}')
            _write(output_dir / "changelog.semantic_payload.json", '{"schema_version":"semantic"}')
            _write(output_dir / "changelog.context.json", '{"schema_version":"context"}')

            members = self._valid_debian_members()
            members.remove("usr/share/doc/vaulthalla/LICENSE.gz")
            members.add("usr/share/doc/vaulthalla/LICENSE")
            with patch("tools.release.packaging.debian._read_debian_package_members", return_value=members):
                result = validate_release_artifacts(output_dir=output_dir, require_changelog=True)

            self.assertEqual(len(result.debian_artifacts), 1)

    def _stage_valid_release(self, output_dir: Path) -> None:
        output_dir.mkdir(parents=True, exist_ok=True)
        _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "deb")
        self._write_valid_web_archive(output_dir / "vaulthalla-web_1.2.3-1_next-standalone.tar.gz")

    def test_validation_does_not_require_static_libraries(self) -> None:
        members = self._valid_debian_members()
        self.assertFalse(any(member.endswith(".a") for member in members))
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            self._stage_valid_release(output_dir)
            with patch("tools.release.packaging.debian._read_debian_package_members", return_value=members):
                result = validate_release_artifacts(output_dir=output_dir, require_changelog=False)
            self.assertEqual(len(result.debian_artifacts), 1)

    def test_validation_rejects_retired_cli_units(self) -> None:
        for unit in ("vaulthalla-cli.socket", "vaulthalla-cli.service"):
            with self.subTest(unit=unit), TemporaryDirectory() as temp_dir:
                output_dir = Path(temp_dir) / "release"
                self._stage_valid_release(output_dir)
                members = self._valid_debian_members()
                members.add(f"lib/systemd/system/{unit}")
                with (
                    patch("tools.release.packaging.debian._read_debian_package_members", return_value=members),
                    self.assertRaisesRegex(ValueError, rf"ships retired .lib/systemd/system/{unit}."),
                ):
                    _ = validate_release_artifacts(output_dir=output_dir, require_changelog=False)

    def test_validation_requires_default_config_under_usr_share(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            self._stage_valid_release(output_dir)
            members = self._valid_debian_members()
            members.remove("usr/share/vaulthalla/config/config.yaml")
            members.add("etc/vaulthalla/config.yaml")
            with (
                patch("tools.release.packaging.debian._read_debian_package_members", return_value=members),
                self.assertRaisesRegex(ValueError, r"usr/share/vaulthalla/config/config\.yaml"),
            ):
                _ = validate_release_artifacts(output_dir=output_dir, require_changelog=False)

    def test_validation_accepts_shipped_config_identical_to_reference(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            self._stage_valid_release(output_dir)
            reference = Path(temp_dir) / "deploy" / "config" / "config.yaml"
            _write(reference, "server:\n  port: 36969\n")
            with (
                patch(
                    "tools.release.packaging.debian._read_debian_package_members",
                    return_value=self._valid_debian_members(),
                ),
                patch(
                    "tools.release.packaging.debian._read_debian_package_file",
                    return_value=b"server:\n  port: 36969\n",
                ) as read_file,
            ):
                result = validate_release_artifacts(
                    output_dir=output_dir, require_changelog=False, reference_config=reference
                )
            self.assertEqual(len(result.debian_artifacts), 1)
            self.assertEqual(read_file.call_args.args[1], "usr/share/vaulthalla/config/config.yaml")

    def test_validation_rejects_shipped_config_that_differs_from_reference(self) -> None:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            self._stage_valid_release(output_dir)
            reference = Path(temp_dir) / "deploy" / "config" / "config.yaml"
            _write(reference, "server:\n  port: 36969\n")
            with (
                patch(
                    "tools.release.packaging.debian._read_debian_package_members",
                    return_value=self._valid_debian_members(),
                ),
                patch(
                    "tools.release.packaging.debian._read_debian_package_file",
                    return_value=b"server:\n  port: 36969\nprivate_override: true\n",
                ),
                self.assertRaisesRegex(ValueError, "not byte-identical"),
            ):
                _ = validate_release_artifacts(
                    output_dir=output_dir, require_changelog=False, reference_config=reference
                )

    def test_validation_emits_sha256sums_when_missing(self) -> None:
        from tools.release.packaging.checksums import read_sha256sums, sha256_file

        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            self._stage_valid_release(output_dir)
            with patch(
                "tools.release.packaging.debian._read_debian_package_members",
                return_value=self._valid_debian_members(),
            ):
                result = validate_release_artifacts(output_dir=output_dir, require_changelog=False)
            self.assertTrue(result.checksums_generated)
            entries = read_sha256sums(output_dir / "SHA256SUMS")
            self.assertEqual(
                entries["vaulthalla_1.2.3-1_amd64.deb"],
                sha256_file(output_dir / "vaulthalla_1.2.3-1_amd64.deb"),
            )
            self.assertIn("vaulthalla-web_1.2.3-1_next-standalone.tar.gz", entries)

    def test_validation_fails_when_artifact_changed_after_checksums(self) -> None:
        from tools.release.packaging.checksums import write_sha256sums

        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "release"
            self._stage_valid_release(output_dir)
            write_sha256sums(output_dir)
            _write(output_dir / "vaulthalla_1.2.3-1_amd64.deb", "tampered")
            with (
                patch(
                    "tools.release.packaging.debian._read_debian_package_members",
                    return_value=self._valid_debian_members(),
                ),
                self.assertRaisesRegex(ValueError, "SHA256SUMS does not match"),
            ):
                _ = validate_release_artifacts(output_dir=output_dir, require_changelog=False)


if __name__ == "__main__":
    unittest.main()
