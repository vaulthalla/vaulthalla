import unittest
import subprocess
import tempfile
from pathlib import Path
from unittest import mock

from deploy.lifecycle import main


class ConfigProjectionTests(unittest.TestCase):
    def test_parse_projection_reads_nested_sections(self) -> None:
        text = """
websocket_server:
  enabled: false
  host: 127.0.0.1
  port: 36969
http_preview_server:
  enabled: true
  host: ::1
  port: 36970
s3_gateway:
  enabled: true
  host: 0.0.0.0
  port: 39000
database:
  host: db.example.net
  port: 5433
  name: vault
  user: vault_user
  pool_size: 42
"""
        projection = main.parse_config_projection_from_text(text)
        self.assertEqual(projection["websocket_server"]["enabled"], False)
        self.assertEqual(projection["websocket_server"]["host"], "127.0.0.1")
        self.assertEqual(projection["websocket_server"]["port"], 36969)
        self.assertEqual(projection["http_preview_server"]["host"], "::1")
        self.assertEqual(projection["s3_gateway"]["enabled"], True)
        self.assertEqual(projection["s3_gateway"]["host"], "0.0.0.0")
        self.assertEqual(projection["s3_gateway"]["port"], 39000)
        self.assertEqual(projection["database"]["host"], "db.example.net")
        self.assertEqual(projection["database"]["pool_size"], 42)

    def test_parse_projection_uses_s3_defaults_when_section_missing(self) -> None:
        projection = main.parse_config_projection_from_text("database:\n  host: localhost\n")
        self.assertEqual(projection["s3_gateway"], {"enabled": False, "host": "0.0.0.0", "port": 39000})

    def test_render_managed_nginx_config_includes_dedicated_https_s3_host(self) -> None:
        projection = {
            "websocket_server": {"enabled": True, "host": "127.0.0.1", "port": 36969},
            "http_preview_server": {"enabled": True, "host": "::", "port": 36970},
            "s3_gateway": {"enabled": False, "host": "0.0.0.0", "port": 39000},
            "database": {"host": "localhost", "port": 5432, "name": "vaulthalla", "user": "vaulthalla", "pool_size": 10},
        }
        rendered = main.render_managed_nginx_config(
            projection,
            "vaulthalla.dev",
            "s3.vaulthalla.dev",
            "vaulthalla.dev",
        )

        self.assertIn("    server_name vaulthalla.dev s3.vaulthalla.dev;\n", rendered)
        self.assertIn("    return 308 https://$host$request_uri;\n", rendered)
        self.assertIn("    server_name vaulthalla.dev;\n", rendered)
        self.assertIn("    server_name s3.vaulthalla.dev;\n", rendered)
        self.assertIn("    ssl_certificate /etc/letsencrypt/live/vaulthalla.dev/fullchain.pem;\n", rendered)
        self.assertIn("    ssl_certificate_key /etc/letsencrypt/live/vaulthalla.dev/privkey.pem;\n", rendered)
        self.assertIn("        proxy_pass http://127.0.0.1:39000;\n", rendered)
        self.assertNotIn("proxy_pass http://127.0.0.1:39000/;", rendered)
        self.assertIn("        proxy_request_buffering off;\n", rendered)
        self.assertIn("        proxy_set_header Host $http_host;\n", rendered)
        self.assertIn("        proxy_set_header X-Vaulthalla-S3-Path-Style-Only true;\n", rendered)
        self.assertNotIn("/api/s3", rendered)

    def test_render_managed_nginx_config_uses_configured_s3_loopback_safe_host(self) -> None:
        projection = {
            "websocket_server": {"enabled": False, "host": "127.0.0.1", "port": 36969},
            "http_preview_server": {"enabled": False, "host": "127.0.0.1", "port": 36970},
            "s3_gateway": {"enabled": False, "host": "::", "port": 39123},
            "database": {"host": "localhost", "port": 5432, "name": "vaulthalla", "user": "vaulthalla", "pool_size": 10},
        }
        rendered = main.render_managed_nginx_config(
            projection,
            "vaulthalla.dev",
            "s3.vaulthalla.dev",
            "vaulthalla.dev",
        )
        self.assertIn("        proxy_pass http://127.0.0.1:39123;\n", rendered)

    def test_render_managed_nginx_config_without_cert_does_not_emit_s3_route(self) -> None:
        projection = {
            "websocket_server": {"enabled": False, "host": "127.0.0.1", "port": 36969},
            "http_preview_server": {"enabled": False, "host": "127.0.0.1", "port": 36970},
            "s3_gateway": {"enabled": False, "host": "0.0.0.0", "port": 39000},
            "database": {"host": "localhost", "port": 5432, "name": "vaulthalla", "user": "vaulthalla", "pool_size": 10},
        }
        rendered = main.render_managed_nginx_config(projection, "vaulthalla.dev", "s3.vaulthalla.dev")

        self.assertIn("    server_name vaulthalla.dev;\n", rendered)
        self.assertNotIn("server_name s3.vaulthalla.dev", rendered)
        self.assertNotIn("127.0.0.1:39000", rendered)
        self.assertNotIn("/api/s3", rendered)

    def test_cloudflare_credentials_file_requires_private_mode(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            credentials = Path(tmp) / "cloudflare.ini"
            credentials.write_text("dns_cloudflare_api_token = token\n", encoding="utf-8")
            credentials.chmod(0o600)
            self.assertEqual(main.validate_cloudflare_credentials_file(str(credentials)), credentials)

            credentials.chmod(0o644)
            with self.assertRaises(main.LifecycleError):
                main.validate_cloudflare_credentials_file(str(credentials))

    def test_request_dns_cloudflare_certificate_builds_multi_domain_command(self) -> None:
        cp = subprocess.CompletedProcess(["certbot"], 0, stdout="", stderr="")
        with mock.patch.object(main, "run_capture", return_value=cp) as run:
            mode = main.request_dns_cloudflare_certificate(
                "vaulthalla.dev",
                "s3.vaulthalla.dev",
                Path("/etc/vaulthalla/certbot/cloudflare.ini"),
            )

        self.assertEqual(mode, "dns-cloudflare certificate request completed")
        command = run.call_args.args[0]
        self.assertEqual(command[0:2], ["certbot", "certonly"])
        self.assertIn("--dns-cloudflare", command)
        self.assertIn("--expand", command)
        self.assertIn("--cert-name", command)
        self.assertIn("vaulthalla.dev", command)
        self.assertIn("s3.vaulthalla.dev", command)

    def test_update_database_block_rewrites_existing_keys(self) -> None:
        text = """
database:
  host: localhost
  port: 5432
  name: vaulthalla
  user: vaulthalla

sharing:
  enabled: true
"""
        updated = main.update_database_block_text(
            text,
            {
                "host": "db.example.net",
                "port": 5433,
                "name": "vault",
                "user": "vault_user",
                "pool_size": 15,
            },
        )
        self.assertIn("  host: db.example.net\n", updated)
        self.assertIn("  port: 5433\n", updated)
        self.assertIn("  name: vault\n", updated)
        self.assertIn("  user: vault_user\n", updated)
        self.assertIn("  pool_size: 15\n", updated)
        self.assertIn("sharing:\n", updated)


class DispatchTests(unittest.TestCase):
    def test_setup_db_dispatches(self) -> None:
        parser = main.build_parser()
        args = parser.parse_args(["setup", "db"])
        with mock.patch.object(main, "setup_db", return_value=0) as handler:
            rc = main.dispatch(args)
        self.assertEqual(rc, 0)
        handler.assert_called_once_with(args)

    def test_teardown_nginx_dispatches(self) -> None:
        parser = main.build_parser()
        args = parser.parse_args(["teardown", "nginx"])
        with mock.patch.object(main, "teardown_nginx", return_value=0) as handler:
            rc = main.dispatch(args)
        self.assertEqual(rc, 0)
        handler.assert_called_once_with(args)


class NginxDomainConflictTests(unittest.TestCase):
    def test_extract_server_names_from_nginx_dump_tracks_active_source_file(self) -> None:
        dump = """
# configuration file /etc/nginx/sites-enabled/default:
server {
    server_name default.example.net www.default.example.net;
}
# configuration file /etc/nginx/sites-enabled/vaulthalla:
server {
    server_name demo.vaulthalla.io;
}
"""
        names = main.extract_server_names_from_nginx_dump(dump)
        self.assertIn(("default.example.net", "/etc/nginx/sites-enabled/default"), names)
        self.assertIn(("www.default.example.net", "/etc/nginx/sites-enabled/default"), names)
        self.assertIn(("demo.vaulthalla.io", "/etc/nginx/sites-enabled/vaulthalla"), names)

    def test_active_nginx_domain_conflict_source_ignores_managed_site(self) -> None:
        dump = """
# configuration file /etc/nginx/sites-enabled/vaulthalla:
server {
    server_name demo.vaulthalla.io;
}
# configuration file /etc/nginx/sites-enabled/custom-site:
server {
    server_name demo.vaulthalla.io;
}
"""
        cp = subprocess.CompletedProcess(["nginx", "-T"], 0, stdout=dump, stderr="")
        with mock.patch.object(main, "run_capture", return_value=cp):
            with mock.patch.object(main, "is_managed_nginx_path", side_effect=lambda p: p.endswith("/vaulthalla")):
                conflict = main.active_nginx_domain_conflict_source("demo.vaulthalla.io")
        self.assertEqual(conflict, "/etc/nginx/sites-enabled/custom-site")

    def test_server_name_matches_domain_handles_exact_and_wildcard(self) -> None:
        self.assertTrue(main.server_name_matches_domain("demo.vaulthalla.io", "demo.vaulthalla.io"))
        self.assertTrue(main.server_name_matches_domain("*.vaulthalla.io", "demo.vaulthalla.io"))
        self.assertFalse(main.server_name_matches_domain("*.vaulthalla.io", "vaulthalla.io"))
        self.assertFalse(main.server_name_matches_domain("_", "demo.vaulthalla.io"))


if __name__ == "__main__":
    unittest.main()
