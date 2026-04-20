from __future__ import annotations

from pathlib import Path
import unittest


class DebianRulesContractTests(unittest.TestCase):
    def test_debian_rules_uses_core_as_meson_source_directory(self) -> None:
        repo_root = Path(__file__).resolve().parents[4]
        rules_path = repo_root / "debian" / "rules"
        rules = rules_path.read_text(encoding="utf-8")

        self.assertIn(
            "dh_auto_configure --sourcedirectory=core -- -Dmanpage=true",
            rules,
        )
        self.assertIn(
            "dh_auto_install --sourcedirectory=core --destdir=debian/tmp",
            rules,
        )

    def test_debian_rules_stages_non_meson_runtime_payloads(self) -> None:
        repo_root = Path(__file__).resolve().parents[4]
        rules = (repo_root / "debian" / "rules").read_text(encoding="utf-8")

        required_fragments = (
            "install -m 0644 deploy/config/config.yaml debian/tmp/etc/vaulthalla/config.yaml",
            "install -m 0644 deploy/config/config_template.yaml.in debian/tmp/etc/vaulthalla/config_template.yaml.in",
            "sed 's|@BINDIR@|/usr/bin|g' deploy/systemd/vaulthalla.service.in > debian/tmp/lib/systemd/system/vaulthalla.service",
            "sed 's|@BINDIR@|/usr/bin|g' deploy/systemd/vaulthalla-cli.service.in > debian/tmp/lib/systemd/system/vaulthalla-cli.service",
            "install -m 0644 deploy/systemd/vaulthalla-cli.socket debian/tmp/lib/systemd/system/vaulthalla-cli.socket",
            "install -m 0644 LICENSE debian/tmp/usr/share/doc/vaulthalla/LICENSE",
            "install -m 0644 debian/copyright debian/tmp/usr/share/doc/vaulthalla/copyright",
            "ln -sf vaulthalla-cli debian/tmp/usr/bin/vaulthalla",
            "ln -sf vaulthalla-cli debian/tmp/usr/bin/vh",
            "install -m 0644 debian/vaulthalla.udev debian/tmp/usr/lib/$(DEB_HOST_MULTIARCH)/udev/rules.d/60-vaulthalla-tpm.rules",
            "install -m 0644 debian/tmpfiles.d/vaulthalla.conf debian/tmp/usr/lib/$(DEB_HOST_MULTIARCH)/tmpfiles.d/vaulthalla.conf",
        )
        for fragment in required_fragments:
            self.assertIn(fragment, rules)

    def test_debian_install_uses_multiarch_globs(self) -> None:
        repo_root = Path(__file__).resolve().parents[4]
        install_manifest = (repo_root / "debian" / "install").read_text(encoding="utf-8")

        self.assertIn("usr/lib/*/libvaulthalla.a usr/lib/libvaulthalla.a", install_manifest)
        self.assertIn("usr/lib/*/libvhusage.a usr/lib/libvhusage.a", install_manifest)
        self.assertIn(
            "usr/lib/*/udev/rules.d/60-vaulthalla-tpm.rules usr/lib/udev/rules.d/60-vaulthalla-tpm.rules",
            install_manifest,
        )
        self.assertIn(
            "usr/lib/*/tmpfiles.d/vaulthalla.conf usr/lib/tmpfiles.d/",
            install_manifest,
        )


if __name__ == "__main__":
    unittest.main()
