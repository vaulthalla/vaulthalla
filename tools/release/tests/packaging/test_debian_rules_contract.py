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


if __name__ == "__main__":
    unittest.main()
