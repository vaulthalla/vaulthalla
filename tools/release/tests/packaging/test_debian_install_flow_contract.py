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
            "ensure_fuse_allow_other()",
            "FUSE config: enabled user_allow_other in /etc/fuse.conf",
            "configure_swtpm_apparmor_override_if_possible",
            "SWTPM_BASE_DIR=\"/var/lib/swtpm\"",
            "SWTPM_STATE_DIR=\"${SWTPM_BASE_DIR}/vaulthalla\"",
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
        self.assertIn("swtpm,", control)
        self.assertIn("swtpm-tools", control)
        self.assertNotIn("Depends:\n adduser,\n nodejs,\n postgresql,", control)

    def test_readme_documents_phase3_cli_integration_followups(self) -> None:
        readme = (self._repo_root() / "debian" / "README.Debian").read_text(encoding="utf-8")
        required = (
            "apt install vaulthalla",
            "apt install --no-install-recommends vaulthalla",
            "VH_SKIP_DB_BOOTSTRAP=1 sudo -E apt install vaulthalla",
            "VH_SKIP_NGINX_CONFIG=1 sudo -E apt install vaulthalla",
            "vh setup db",
            "vh setup remote-db",
            "vh setup nginx",
            "vh setup nginx --certbot --domain",
            "vh teardown nginx",
            "canonical final deployment path",
            "/usr/share/vaulthalla/psql",
            "/var/lib/vaulthalla/nginx_site_managed",
            "TPM backend behavior",
            "systemctl status vaulthalla-swtpm.service",
        )
        for fragment in required:
            self.assertIn(fragment, readme)

    def test_top_level_readme_matches_low_prompt_install_contract(self) -> None:
        readme = (self._repo_root() / "README.md").read_text(encoding="utf-8")
        required = (
            "sudo apt install vaulthalla",
            "sudo apt install --no-install-recommends vaulthalla",
            "VH_SKIP_DB_BOOTSTRAP=1 sudo -E apt install vaulthalla",
            "VH_SKIP_NGINX_CONFIG=1 sudo -E apt install vaulthalla",
            "vh setup db",
            "vh setup remote-db",
            "vh setup nginx",
            "sudo vh setup nginx --domain <domain> --certbot",
            "vh teardown nginx",
            "The CLI is the control plane.",
        )
        forbidden = (
            "Debian Install Prompts",
            "Initialize PostgreSQL database?",
            "Super-admin Linux UID",
        )
        for fragment in required:
            self.assertIn(fragment, readme)
        for fragment in forbidden:
            self.assertNotIn(fragment, readme)

    def test_shell_usage_and_command_registry_include_setup_and_teardown(self) -> None:
        repo = self._repo_root()
        usages_hpp = (repo / "core" / "usage" / "include" / "usages.hpp").read_text(encoding="utf-8")
        usage_manager = (repo / "core" / "usage" / "src" / "UsageManager.cpp").read_text(encoding="utf-8")
        commands_hpp = (repo / "core" / "include" / "protocols" / "shell" / "commands" / "all.hpp").read_text(encoding="utf-8")
        commands_cpp = (repo / "core" / "src" / "protocols" / "shell" / "commands" / "all.cpp").read_text(encoding="utf-8")

        self.assertIn("namespace setup", usages_hpp)
        self.assertIn("namespace teardown", usages_hpp)
        self.assertIn("registerBook(setup::get", usage_manager)
        self.assertIn("registerBook(teardown::get", usage_manager)
        self.assertIn("registerSetupCommands", commands_hpp)
        self.assertIn("registerTeardownCommands", commands_hpp)
        self.assertIn("registerSetupCommands(r);", commands_cpp)
        self.assertIn("registerTeardownCommands(r);", commands_cpp)


if __name__ == "__main__":
    unittest.main()
