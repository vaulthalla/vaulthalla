#!/usr/bin/env python3
from __future__ import annotations

import argparse
import filecmp
import os
import pwd
import re
import secrets
import shutil
import string
import subprocess
import sys
from pathlib import Path
from typing import Any

DB_NAME = "vaulthalla"
DB_USER = "vaulthalla"
SERVICE_UNIT = "vaulthalla.service"
PENDING_DB_PASSWORD_FILE = Path("/run/vaulthalla/db_password")

DEFAULT_CONFIG_PATH = Path("/etc/vaulthalla/config.yaml")
DEFAULT_SCHEMA_DIR = Path("/usr/share/vaulthalla/psql")
DEFAULT_NGINX_TEMPLATE = Path("/usr/share/vaulthalla/nginx/vaulthalla")

NGINX_SITE_AVAILABLE = Path("/etc/nginx/sites-available/vaulthalla")
NGINX_SITE_ENABLED = Path("/etc/nginx/sites-enabled/vaulthalla")
NGINX_MANAGED_MARKER = Path("/var/lib/vaulthalla/nginx_site_managed")

WEB_UPSTREAM_HOST = "127.0.0.1"
WEB_UPSTREAM_PORT = 36968


class LifecycleError(RuntimeError):
    pass


def eprint(msg: str) -> None:
    print(msg, file=sys.stderr)


def run_capture(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, capture_output=True, text=True, check=False)


def command_exists(command: str) -> bool:
    return shutil.which(command) is not None


def trim(value: str) -> str:
    return value.strip()


def combined_output(result: subprocess.CompletedProcess[str]) -> str:
    out = (result.stdout or "") + (result.stderr or "")
    return trim(out)


def format_failure(step: str, result: subprocess.CompletedProcess[str]) -> str:
    msg = f"{step} (exit {result.returncode})"
    output = combined_output(result)
    if output:
        msg += f": {output}"
    return msg


def config_path() -> Path:
    override = os.environ.get("VAULTHALLA_CONFIG_PATH")
    return Path(override) if override else DEFAULT_CONFIG_PATH


def fallback_deploy_root() -> Path:
    return Path(__file__).resolve().parents[1]


def schema_dir() -> Path:
    if DEFAULT_SCHEMA_DIR.is_dir():
        return DEFAULT_SCHEMA_DIR
    fallback = fallback_deploy_root() / "psql"
    return fallback


def nginx_template_path() -> Path:
    if DEFAULT_NGINX_TEMPLATE.is_file():
        return DEFAULT_NGINX_TEMPLATE
    return fallback_deploy_root() / "nginx" / "vaulthalla.conf"


def ensure_privileged_or_reexec() -> None:
    if os.geteuid() == 0:
        return
    if command_exists("sudo"):
        probe = run_capture(["sudo", "-n", "true"])
        if probe.returncode == 0:
            os.execvp("sudo", ["sudo", "-n", sys.executable, __file__, *sys.argv[1:]])
    raise LifecycleError("requires root privileges (or non-interactive sudo access)")


def parse_scalar(raw: str) -> Any:
    text = trim(raw)
    if text == "":
        return ""
    if text.startswith("'") and text.endswith("'") and len(text) >= 2:
        return text[1:-1].replace("''", "'")
    if text.startswith('"') and text.endswith('"') and len(text) >= 2:
        return text[1:-1]
    if text in ("true", "false"):
        return text == "true"
    if re.fullmatch(r"-?\d+", text):
        return int(text)
    return text


def parse_config_projection_from_text(text: str) -> dict[str, dict[str, Any]]:
    projection: dict[str, dict[str, Any]] = {
        "websocket_server": {"enabled": True, "host": "0.0.0.0", "port": 33369},
        "http_preview_server": {"enabled": True, "host": "0.0.0.0", "port": 33370},
        "database": {"host": "localhost", "port": 5432, "name": DB_NAME, "user": DB_USER, "pool_size": 10},
    }
    section: str | None = None
    for raw_line in text.splitlines():
        line = raw_line.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        if not raw_line.startswith((" ", "\t")):
            m = re.match(r"^([A-Za-z_][A-Za-z0-9_-]*):\s*$", line)
            section = m.group(1) if m else None
            continue
        if section not in projection:
            continue
        m = re.match(r"^\s{2}([A-Za-z_][A-Za-z0-9_-]*):\s*(.*?)\s*$", line)
        if not m:
            continue
        key = m.group(1)
        projection[section][key] = parse_scalar(m.group(2))
    return projection


def load_config_projection(path: Path) -> dict[str, dict[str, Any]]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise LifecycleError(f"failed loading config '{path}': {exc}") from exc
    return parse_config_projection_from_text(text)


def yaml_scalar(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    text = str(value)
    if re.fullmatch(r"[A-Za-z0-9_.:/-]+", text):
        return text
    return "'" + text.replace("'", "''") + "'"


def update_database_block_text(text: str, updates: dict[str, Any]) -> str:
    lines = text.splitlines(keepends=True)
    start = -1
    for i, line in enumerate(lines):
        if line.startswith((" ", "\t")):
            continue
        stripped = line.split("#", 1)[0].strip()
        if stripped == "database:":
            start = i
            break

    key_order = ("host", "port", "name", "user", "pool_size")
    if start == -1:
        if lines and not lines[-1].endswith("\n"):
            lines[-1] += "\n"
        if lines and trim(lines[-1]):
            lines.append("\n")
        lines.append("database:\n")
        for key in key_order:
            if key in updates:
                lines.append(f"  {key}: {yaml_scalar(updates[key])}\n")
        return "".join(lines)

    end = len(lines)
    for j in range(start + 1, len(lines)):
        candidate = lines[j]
        if candidate.startswith((" ", "\t")):
            continue
        stripped = candidate.split("#", 1)[0].strip()
        if re.match(r"^[A-Za-z_][A-Za-z0-9_-]*:\s*", stripped):
            end = j
            break

    block = lines[start + 1:end]
    for key in key_order:
        if key not in updates:
            continue
        replaced = False
        for idx, block_line in enumerate(block):
            if re.match(rf"^\s{{2}}{re.escape(key)}\s*:", block_line):
                block[idx] = f"  {key}: {yaml_scalar(updates[key])}\n"
                replaced = True
                break
        if not replaced:
            block.append(f"  {key}: {yaml_scalar(updates[key])}\n")

    return "".join(lines[: start + 1] + block + lines[end:])


def save_database_config_updates(path: Path, updates: dict[str, Any]) -> None:
    try:
        original = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise LifecycleError(f"failed loading config '{path}': {exc}") from exc

    updated = update_database_block_text(original, updates)
    try:
        path.write_text(updated, encoding="utf-8")
    except OSError as exc:
        raise LifecycleError(f"failed saving config '{path}': {exc}") from exc


def make_db_password(length: int = 48) -> str:
    alphabet = string.ascii_letters + string.digits
    return "".join(secrets.choice(alphabet) for _ in range(length))


def choose_postgres_prefix() -> list[str]:
    candidates: list[list[str]] = []
    if command_exists("runuser"):
        candidates.append(["runuser", "-u", "postgres", "--", "psql", "-X", "-v", "ON_ERROR_STOP=1"])
    if command_exists("sudo"):
        probe = run_capture(["sudo", "-n", "true"])
        if probe.returncode == 0:
            candidates.append(["sudo", "-n", "-u", "postgres", "psql", "-X", "-v", "ON_ERROR_STOP=1"])

    for prefix in candidates:
        probe = run_capture(prefix + ["-d", "postgres", "-tAc", "SELECT 1;"])
        if probe.returncode == 0 and trim(probe.stdout) == "1":
            return prefix
    raise LifecycleError(
        "unable to run PostgreSQL admin commands as 'postgres'. "
        "Verify local PostgreSQL is installed/running and root-equivalent privileges are available."
    )


def psql_sql(prefix: list[str], db: str, sql: str) -> subprocess.CompletedProcess[str]:
    return run_capture(prefix + ["-d", db, "-tAc", sql])


def write_pending_db_password(password: str) -> None:
    try:
        account = pwd.getpwnam(DB_USER)
    except KeyError as exc:
        raise LifecycleError(f"system user '{DB_USER}' not found") from exc

    try:
        PENDING_DB_PASSWORD_FILE.parent.mkdir(parents=True, exist_ok=True)
        PENDING_DB_PASSWORD_FILE.write_text(password + "\n", encoding="utf-8")
        os.chown(PENDING_DB_PASSWORD_FILE, account.pw_uid, account.pw_gid)
        os.chmod(PENDING_DB_PASSWORD_FILE, 0o600)
    except OSError as exc:
        raise LifecycleError(f"failed preparing pending DB password handoff file: {exc}") from exc


def load_password_from_file(path: str) -> str:
    source = Path(path)
    if not source.exists():
        raise LifecycleError(f"password file does not exist: {source}")
    if not source.is_file():
        raise LifecycleError(f"password file is not a regular file: {source}")
    try:
        content = source.read_text(encoding="utf-8")
    except OSError as exc:
        raise LifecycleError(f"failed opening password file: {source}") from exc
    first = content.strip().split()
    if not first:
        raise LifecycleError(f"password file is empty or invalid: {source}")
    return first[0]


def restart_or_start_service() -> str:
    if not command_exists("systemctl"):
        raise LifecycleError("systemctl is not available; cannot hand off lifecycle updates to runtime startup")
    active = run_capture(["systemctl", "--quiet", "is-active", SERVICE_UNIT])
    action = "restart" if active.returncode == 0 else "start"
    result = run_capture(["systemctl", action, SERVICE_UNIT])
    if result.returncode != 0:
        raise LifecycleError(format_failure(f"failed to {action} {SERVICE_UNIT}", result))
    return "restarted" if action == "restart" else "started"


def validate_and_reload_nginx() -> str:
    test = run_capture(["nginx", "-t"])
    if test.returncode != 0:
        output = combined_output(test)
        raise LifecycleError("nginx -t failed" + (f": {output}" if output else ""))
    if command_exists("systemctl"):
        active = run_capture(["systemctl", "--quiet", "is-active", "nginx.service"])
        if active.returncode == 0:
            reload_result = run_capture(["systemctl", "reload", "nginx.service"])
            if reload_result.returncode != 0:
                raise LifecycleError(format_failure("systemctl reload nginx.service", reload_result))
            return "nginx reloaded"
    return "not attempted (systemctl unavailable or nginx inactive)"


def is_likely_domain(raw: str) -> bool:
    if not raw or len(raw) > 253:
        return False
    if raw[0] in ".-" or raw[-1] in ".-":
        return False
    labels = raw.split(".")
    if len(labels) < 2:
        return False
    for label in labels:
        if not label or len(label) > 63:
            return False
        if not re.fullmatch(r"[A-Za-z0-9-]+", label):
            return False
    return True


def has_non_nginx_listeners_on_web_ports() -> bool:
    if not command_exists("ss"):
        return False
    listeners = run_capture(["ss", "-H", "-ltnp", "( sport = :80 or sport = :443 )"])
    if listeners.returncode != 0:
        return False
    for line in listeners.stdout.splitlines():
        if trim(line) and "nginx" not in line:
            return True
    return False


def has_custom_nginx_sites_enabled() -> bool:
    sites_enabled = Path("/etc/nginx/sites-enabled")
    if not sites_enabled.is_dir():
        return False
    for entry in sites_enabled.iterdir():
        if entry.name not in ("default", "vaulthalla"):
            return True
    return False


def normalize_local_upstream_host(host: str) -> str:
    value = trim(host)
    if value in ("", "0.0.0.0", "::", "[::]"):
        return "127.0.0.1"
    return value


def host_for_proxy_pass(host: str) -> str:
    value = normalize_local_upstream_host(host)
    if ":" in value and not value.startswith("["):
        return f"[{value}]"
    return value


def render_managed_nginx_config(projection: dict[str, dict[str, Any]], domain: str | None) -> str:
    server_name = domain if domain else "_"
    websocket = projection["websocket_server"]
    preview = projection["http_preview_server"]
    ws_host = host_for_proxy_pass(str(websocket.get("host", "0.0.0.0")))
    preview_host = host_for_proxy_pass(str(preview.get("host", "0.0.0.0")))

    out: list[str] = []
    out.append("# Generated by 'vh setup nginx'.")
    out.append("# Do not edit manually; rerun CLI setup to apply managed changes.")
    out.append("server {")
    out.append("    listen 80;")
    out.append("    listen [::]:80;")
    out.append(f"    server_name {server_name};")
    out.append("")

    if bool(websocket.get("enabled", True)):
        ws_port = int(websocket.get("port", 33369))
        out.extend([
            "    location /ws {",
            f"        proxy_pass http://{ws_host}:{ws_port};",
            "        proxy_http_version 1.1;",
            "        proxy_set_header Host $host;",
            "        proxy_set_header X-Real-IP $remote_addr;",
            "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;",
            "        proxy_set_header X-Forwarded-Proto $scheme;",
            "        proxy_set_header Upgrade $http_upgrade;",
            "        proxy_set_header Connection \"upgrade\";",
            "    }",
            "",
        ])

    if bool(preview.get("enabled", True)):
        preview_port = int(preview.get("port", 33370))
        out.extend([
            "    location /preview {",
            f"        proxy_pass http://{preview_host}:{preview_port};",
            "        proxy_http_version 1.1;",
            "        proxy_set_header Host $host;",
            "        proxy_set_header X-Real-IP $remote_addr;",
            "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;",
            "        proxy_set_header X-Forwarded-Proto $scheme;",
            "    }",
            "",
        ])

    out.extend([
        "    location / {",
        f"        proxy_pass http://{WEB_UPSTREAM_HOST}:{WEB_UPSTREAM_PORT};",
        "        proxy_http_version 1.1;",
        "        proxy_set_header Host $host;",
        "        proxy_set_header X-Real-IP $remote_addr;",
        "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;",
        "        proxy_set_header X-Forwarded-Proto $scheme;",
        "        proxy_set_header Upgrade $http_upgrade;",
        "        proxy_set_header Connection \"upgrade\";",
        "    }",
        "}",
        "",
    ])
    return "\n".join(out)


def is_managed_site_symlink_target(path: Path) -> bool:
    if not path.is_symlink():
        return False
    target = os.readlink(path)
    return target in (str(NGINX_SITE_AVAILABLE), "../sites-available/vaulthalla")


def has_cert_for_domain(domain: str) -> bool:
    live = Path("/etc/letsencrypt/live") / domain
    return (live / "fullchain.pem").exists() and (live / "privkey.pem").exists()


def ensure_certbot_prereqs() -> None:
    if not command_exists("certbot"):
        raise LifecycleError("certbot is not installed")
    plugins = run_capture(["certbot", "plugins"])
    if plugins.returncode != 0 or "nginx" not in combined_output(plugins):
        raise LifecycleError("certbot nginx plugin is not installed (expected python3-certbot-nginx)")


def setup_db(_args: argparse.Namespace) -> int:
    schemas = schema_dir()
    if not schemas.is_dir():
        raise LifecycleError(f"canonical schema path is missing: {schemas}")
    sql_files = sorted([p for p in schemas.iterdir() if p.suffix == ".sql" and p.is_file()], key=lambda p: p.name)
    if not sql_files:
        raise LifecycleError(f"canonical schema path has no .sql files: {schemas}")
    if not command_exists("psql"):
        raise LifecycleError("PostgreSQL client 'psql' is not installed")

    prefix = choose_postgres_prefix()
    role_state = psql_sql(prefix, "postgres", f"SELECT 1 FROM pg_roles WHERE rolname = '{DB_USER}';")
    if role_state.returncode != 0:
        raise LifecycleError(format_failure("failed querying PostgreSQL role state", role_state))
    role_exists = trim(role_state.stdout) == "1"

    db_state = psql_sql(prefix, "postgres", f"SELECT 1 FROM pg_database WHERE datname = '{DB_NAME}';")
    if db_state.returncode != 0:
        raise LifecycleError(format_failure("failed querying PostgreSQL database state", db_state))
    db_exists = trim(db_state.stdout) == "1"

    created_role = False
    created_db = False
    generated_password = ""

    if not role_exists:
        generated_password = make_db_password()
        create_role = psql_sql(prefix, "postgres", f"CREATE ROLE {DB_USER} LOGIN PASSWORD '{generated_password}';")
        if create_role.returncode != 0:
            raise LifecycleError(format_failure(f"failed creating PostgreSQL role '{DB_USER}'", create_role))
        created_role = True

    if not db_exists:
        create_db = psql_sql(prefix, "postgres", f"CREATE DATABASE {DB_NAME} OWNER {DB_USER};")
        if create_db.returncode != 0:
            raise LifecycleError(format_failure(f"failed creating PostgreSQL database '{DB_NAME}'", create_db))
        created_db = True

    grant_db = psql_sql(prefix, "postgres", f"GRANT ALL PRIVILEGES ON DATABASE {DB_NAME} TO {DB_USER};")
    if grant_db.returncode != 0:
        raise LifecycleError(format_failure("failed granting database privileges", grant_db))

    grant_schema = psql_sql(prefix, DB_NAME, f"GRANT USAGE, CREATE ON SCHEMA public TO {DB_USER};")
    if grant_schema.returncode != 0:
        raise LifecycleError(format_failure("failed granting schema privileges", grant_schema))

    if created_role:
        write_pending_db_password(generated_password)

    action = restart_or_start_service()

    print("setup db: local PostgreSQL bootstrap complete")
    print(f"  role: {'created' if created_role else 'already existed'} ({DB_USER})")
    print(f"  database: {'created' if created_db else 'already existed'} ({DB_NAME})")
    print(f"  canonical schema path: {schemas} (validated)")
    if created_role:
        print(f"  seeded runtime DB password: {PENDING_DB_PASSWORD_FILE} (owner/mode verified)")
    else:
        print("  seeded runtime DB password: unchanged (existing role/password path)")
    print(f"  service: {SERVICE_UNIT} {action}")
    print("  migrations: delegated to normal runtime startup flow (SqlDeployer)")
    return 0


def setup_remote_db(args: argparse.Namespace) -> int:
    if args.port <= 0 or args.port > 65535:
        raise LifecycleError(f"invalid port '{args.port}' (expected integer 1-65535)")
    if args.pool_size is not None and args.pool_size <= 0:
        raise LifecycleError(f"invalid pool size '{args.pool_size}' (expected positive integer)")

    path = config_path()
    projection = load_config_projection(path)
    existing_pool = int(projection["database"].get("pool_size", 10))
    pool_size = args.pool_size if args.pool_size is not None else existing_pool

    updates = {
        "host": args.host,
        "port": args.port,
        "name": args.database,
        "user": args.user,
        "pool_size": pool_size,
    }
    save_database_config_updates(path, updates)

    password = load_password_from_file(args.password_file)
    write_pending_db_password(password)
    action = restart_or_start_service()

    print("setup remote-db: remote PostgreSQL configuration applied")
    print(f"  config file: {path}")
    print(f"  database.host: {updates['host']}")
    print(f"  database.port: {updates['port']}")
    print(f"  database.user: {updates['user']}")
    print(f"  database.name: {updates['name']}")
    print(f"  database.pool_size: {updates['pool_size']}")
    print(f"  seeded runtime DB password: {PENDING_DB_PASSWORD_FILE} (owner/mode verified)")
    print(f"  service: {SERVICE_UNIT} {action}")
    print("  migrations: delegated to normal runtime startup flow (SqlDeployer)")
    return 0


def setup_nginx(args: argparse.Namespace) -> int:
    if args.certbot and not args.domain:
        raise LifecycleError("--certbot requires --domain <domain>")
    if not args.certbot and args.domain:
        raise LifecycleError("--domain requires --certbot")
    domain = trim(args.domain) if args.domain else None
    if domain and not is_likely_domain(domain):
        raise LifecycleError(f"invalid domain '{domain}'")

    if not command_exists("nginx") or not Path("/etc/nginx").exists():
        raise LifecycleError("nginx is not installed or /etc/nginx is missing")
    if has_non_nginx_listeners_on_web_ports():
        raise LifecycleError("detected non-nginx listeners on :80/:443; refusing automatic integration")
    if has_custom_nginx_sites_enabled() and not NGINX_SITE_ENABLED.exists():
        raise LifecycleError("custom nginx site layout detected; refusing automatic integration")

    template_path = nginx_template_path()
    projection = load_config_projection(config_path())
    rendered = render_managed_nginx_config(projection, domain if args.certbot else None)

    NGINX_SITE_AVAILABLE.parent.mkdir(parents=True, exist_ok=True)
    NGINX_SITE_ENABLED.parent.mkdir(parents=True, exist_ok=True)

    marker_exists = NGINX_MANAGED_MARKER.exists()
    if NGINX_SITE_AVAILABLE.exists() and not marker_exists:
        if not template_path.exists():
            raise LifecycleError(
                f"packaged nginx template missing at {template_path} while existing unmanaged site file is present"
            )
        if not filecmp.cmp(NGINX_SITE_AVAILABLE, template_path, shallow=False):
            raise LifecycleError(f"existing site file differs and is not package-managed: {NGINX_SITE_AVAILABLE}")

    created_site_file = False
    created_site_link = False
    rewrote_site_file = False
    if NGINX_SITE_AVAILABLE.exists():
        current = NGINX_SITE_AVAILABLE.read_text(encoding="utf-8")
        if current != rendered:
            NGINX_SITE_AVAILABLE.write_text(rendered, encoding="utf-8")
            rewrote_site_file = True
    else:
        NGINX_SITE_AVAILABLE.write_text(rendered, encoding="utf-8")
        created_site_file = True
        rewrote_site_file = True

    if not NGINX_MANAGED_MARKER.exists():
        NGINX_MANAGED_MARKER.parent.mkdir(parents=True, exist_ok=True)
        NGINX_MANAGED_MARKER.write_text("managed-by=vaulthalla\n", encoding="utf-8")

    if NGINX_SITE_ENABLED.exists() and not NGINX_SITE_ENABLED.is_symlink():
        raise LifecycleError(f"target exists and is not a symlink: {NGINX_SITE_ENABLED}")
    if NGINX_SITE_ENABLED.is_symlink() and not is_managed_site_symlink_target(NGINX_SITE_ENABLED):
        raise LifecycleError("existing symlink points outside Vaulthalla-managed site")
    if not NGINX_SITE_ENABLED.exists():
        NGINX_SITE_ENABLED.symlink_to(NGINX_SITE_AVAILABLE)
        created_site_link = True

    reload_status = validate_and_reload_nginx()

    certbot_mode = None
    if args.certbot:
        ensure_certbot_prereqs()
        if has_cert_for_domain(domain or ""):
            certbot_mode = "existing certificate detected (renew-safe path)"
            renew = run_capture(["certbot", "renew", "--cert-name", domain or "", "--non-interactive"])
            if renew.returncode != 0:
                raise LifecycleError(format_failure(f"certbot renew --cert-name {domain}", renew))
        else:
            certbot_mode = "no existing certificate detected (fresh issuance path)"
            issue = run_capture([
                "certbot",
                "--nginx",
                "--non-interactive",
                "--agree-tos",
                "--register-unsafely-without-email",
                "--keep-until-expiring",
                "--domain",
                domain or "",
            ])
            if issue.returncode != 0:
                raise LifecycleError(format_failure(f"certbot --nginx --domain {domain}", issue))
        reload_status = validate_and_reload_nginx()

    if not args.certbot:
        print("setup nginx: Vaulthalla nginx integration configured")
        print(
            "  site file: "
            + ("installed" if created_site_file else "regenerated from canonical config" if rewrote_site_file else "already current")
            + f" ({NGINX_SITE_AVAILABLE})"
        )
        print(f"  site link: {'enabled' if created_site_link else 'already enabled'} ({NGINX_SITE_ENABLED})")
        print(f"  config source: {config_path()}")
        print(f"  web upstream: {WEB_UPSTREAM_HOST}:{WEB_UPSTREAM_PORT} (runtime convention)")
        print(f"  reload: {reload_status}")
        return 0

    print("setup nginx: Vaulthalla nginx integration configured with certbot handling")
    print(
        "  site file: "
        + ("installed" if created_site_file else "regenerated from canonical config" if rewrote_site_file else "already present")
        + f" ({NGINX_SITE_AVAILABLE})"
    )
    print(f"  site link: {'enabled' if created_site_link else 'already enabled'} ({NGINX_SITE_ENABLED})")
    print(f"  config source: {config_path()}")
    print(f"  certbot domain: {domain}")
    print(f"  certbot mode: {certbot_mode}")
    print(f"  reload: {reload_status}")
    return 0


def teardown_nginx(_args: argparse.Namespace) -> int:
    removed_link = False
    removed_site = False
    removed_marker = False

    if NGINX_SITE_ENABLED.exists():
        if not NGINX_SITE_ENABLED.is_symlink():
            raise LifecycleError(f"{NGINX_SITE_ENABLED} exists and is not a symlink")
        if not is_managed_site_symlink_target(NGINX_SITE_ENABLED):
            raise LifecycleError("refusing to remove non-Vaulthalla nginx symlink target")
        NGINX_SITE_ENABLED.unlink()
        removed_link = True

    if NGINX_MANAGED_MARKER.exists():
        if NGINX_SITE_AVAILABLE.exists():
            NGINX_SITE_AVAILABLE.unlink()
            removed_site = True
        NGINX_MANAGED_MARKER.unlink()
        removed_marker = True

    if command_exists("nginx") and command_exists("systemctl"):
        active = run_capture(["systemctl", "--quiet", "is-active", "nginx.service"])
        if active.returncode == 0:
            validate_and_reload_nginx()

    print("teardown nginx: completed")
    print(f"  removed enabled symlink: {'yes' if removed_link else 'no'}")
    print(f"  removed site file: {'yes' if removed_site else 'no'}")
    print(f"  removed managed marker: {'yes' if removed_marker else 'no'}")
    return 0


def teardown_db(_args: argparse.Namespace) -> int:
    if not command_exists("psql"):
        raise LifecycleError("PostgreSQL client 'psql' is not installed")
    prefix = choose_postgres_prefix()
    teardown_sql = (
        f"REVOKE CONNECT ON DATABASE {DB_NAME} FROM public; "
        f"SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE datname = '{DB_NAME}'; "
        f"DROP DATABASE IF EXISTS {DB_NAME}; "
        f"DROP ROLE IF EXISTS {DB_USER};"
    )
    result = psql_sql(prefix, "postgres", teardown_sql)
    if result.returncode != 0:
        raise LifecycleError(format_failure("failed tearing down local PostgreSQL role/database", result))

    if PENDING_DB_PASSWORD_FILE.exists():
        PENDING_DB_PASSWORD_FILE.unlink()

    print("teardown db: completed")
    print(f"  dropped database: {DB_NAME}")
    print(f"  dropped role: {DB_USER}")
    print(f"  removed pending password handoff: {'yes' if not PENDING_DB_PASSWORD_FILE.exists() else 'no'}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="vaulthalla-lifecycle",
        description="Local privileged lifecycle utility for Vaulthalla setup/teardown tasks.",
    )
    root = parser.add_subparsers(dest="root", required=True)

    setup_cmd = root.add_parser("setup", help="Lifecycle setup operations")
    setup_sub = setup_cmd.add_subparsers(dest="setup_cmd", required=True)
    setup_sub.add_parser("db", help="Bootstrap local PostgreSQL role/database integration")

    setup_remote = setup_sub.add_parser("remote-db", help="Configure remote PostgreSQL settings in local config")
    setup_remote.add_argument("--host", required=True)
    setup_remote.add_argument("--user", required=True)
    setup_remote.add_argument("--database", required=True)
    setup_remote.add_argument("--password-file", required=True)
    setup_remote.add_argument("--port", type=int, default=5432)
    setup_remote.add_argument("--pool-size", type=int)

    setup_ng = setup_sub.add_parser("nginx", help="Configure Vaulthalla-managed nginx integration")
    setup_ng.add_argument("--certbot", action="store_true")
    setup_ng.add_argument("--domain")

    teardown_cmd = root.add_parser("teardown", help="Lifecycle teardown operations")
    teardown_sub = teardown_cmd.add_subparsers(dest="teardown_cmd", required=True)
    teardown_sub.add_parser("nginx", help="Remove Vaulthalla-managed nginx integration")
    teardown_sub.add_parser("db", help="Drop Vaulthalla local PostgreSQL role/database integration")

    return parser


def dispatch(args: argparse.Namespace) -> int:
    if args.root == "setup" and args.setup_cmd == "db":
        return setup_db(args)
    if args.root == "setup" and args.setup_cmd == "remote-db":
        return setup_remote_db(args)
    if args.root == "setup" and args.setup_cmd == "nginx":
        return setup_nginx(args)
    if args.root == "teardown" and args.teardown_cmd == "nginx":
        return teardown_nginx(args)
    if args.root == "teardown" and args.teardown_cmd == "db":
        return teardown_db(args)
    raise LifecycleError("unsupported lifecycle command")


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    ensure_privileged_or_reexec()
    return dispatch(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except LifecycleError as exc:
        eprint(f"lifecycle error: {exc}")
        raise SystemExit(2)
