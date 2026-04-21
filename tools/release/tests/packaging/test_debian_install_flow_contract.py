from __future__ import annotations

from pathlib import Path
import unittest


class DebianInstallFlowContractTests(unittest.TestCase):
    def _repo_root(self) -> Path:
        return Path(__file__).resolve().parents[4]

    def test_phase2_removes_routine_debconf_scaffolding(self) -> None:
        repo = self._repo_root()
        self.assertFalse((repo / "debian" / "templates").exists())
        self.assertFalse((repo / "debian" / "config").exists())

    def test_postinst_uses_env_overrides_and_no_db_get_prompts(self) -> None:
        postinst = (self._repo_root() / "debian" / "postinst").read_text(encoding="utf-8")
        required_fragments = (
            "VH_SKIP_DB_BOOTSTRAP",
            "VH_SKIP_NGINX_CONFIG",
            "bootstrap_db_if_safe()",
            "configure_nginx_if_safe()",
            "DB_BOOTSTRAP_STATUS=",
            "NGINX_CONFIG_STATUS=",
            "skipped (psql not installed; install PostgreSQL or configure remote DB)",
            "Super-admin ownership: deferred to first CLI use",
        )
        for fragment in required_fragments:
            self.assertIn(fragment, postinst)

        forbidden_fragments = (
            "db_get ",
            "Template:",
            "seed_superadmin_uid",
            "ensure_superadmin_user_in_group",
        )
        for fragment in forbidden_fragments:
            self.assertNotIn(fragment, postinst)

    def test_prerm_and_postrm_cleanup_legacy_superadmin_seed_only_as_legacy(self) -> None:
        repo = self._repo_root()
        prerm = (repo / "debian" / "prerm").read_text(encoding="utf-8")
        postrm = (repo / "debian" / "postrm").read_text(encoding="utf-8")
        self.assertIn("LEGACY_PENDING_SUPERADMIN_UID_FILE", prerm)
        self.assertIn("LEGACY_PENDING_SUPERADMIN_UID_FILE", postrm)
        self.assertNotIn("/usr/share/debconf/confmodule", postrm)
        self.assertNotIn("db_purge", postrm)

    def test_control_uses_recommends_for_postgresql_and_nginx(self) -> None:
        control = (self._repo_root() / "debian" / "control").read_text(encoding="utf-8")
        self.assertIn("Depends:\n adduser,\n nodejs,\n openssl,", control)
        self.assertIn("Recommends:\n postgresql,\n nginx", control)
        self.assertNotIn("Depends:\n adduser,\n nodejs,\n postgresql,", control)


if __name__ == "__main__":
    unittest.main()
