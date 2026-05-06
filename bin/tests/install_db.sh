#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

source "$ROOT_DIR/bin/lib/dev_mode.sh"

vh_require_dev_mode

upsert_env_value() {
  local file="$1"
  local key="$2"
  local value="$3"
  local prefix="${4:-}"

  touch "$file"
  if grep -q "^${prefix}${key}=" "$file"; then
    sed -i "s|^${prefix}${key}=.*|${prefix}${key}=\"${value}\"|" "$file"
  else
    echo "${prefix}${key}=\"${value}\"" >> "$file"
  fi
}

if vh_is_dev_mode; then
  DB_NAME="vh_cli_test"
  DB_ROLE="vaulthalla_test"
  VAUL_TEST_PG_PASS="$(openssl rand -base64 32 | tr -d '\n')"

  if sudo -u postgres psql -v ON_ERROR_STOP=1 -d postgres -tc "SELECT 1 FROM pg_roles WHERE rolname='${DB_ROLE}'" | grep -q 1; then
    sudo -u postgres psql -v ON_ERROR_STOP=1 -d postgres -c "ALTER ROLE ${DB_ROLE} WITH LOGIN PASSWORD '${VAUL_TEST_PG_PASS}';"
  else
    sudo -u postgres psql -v ON_ERROR_STOP=1 -d postgres -c "CREATE ROLE ${DB_ROLE} WITH LOGIN PASSWORD '${VAUL_TEST_PG_PASS}';"
  fi

  if sudo -u postgres psql -v ON_ERROR_STOP=1 -d postgres -tc "SELECT 1 FROM pg_database WHERE datname='${DB_NAME}'" | grep -q 1; then
    sudo -u postgres psql -v ON_ERROR_STOP=1 -d postgres -c "ALTER DATABASE ${DB_NAME} OWNER TO ${DB_ROLE};"
  else
    sudo -u postgres psql -v ON_ERROR_STOP=1 -d postgres -c "CREATE DATABASE ${DB_NAME} OWNER ${DB_ROLE};"
  fi

  sudo -u postgres psql -v ON_ERROR_STOP=1 -d postgres -c "GRANT ALL PRIVILEGES ON DATABASE ${DB_NAME} TO ${DB_ROLE};"
  sudo -u postgres psql -v ON_ERROR_STOP=1 -d "${DB_NAME}" -c "GRANT USAGE, CREATE ON SCHEMA public TO ${DB_ROLE};"

  upsert_env_value "$ROOT_DIR/deploy/bashrc" "VH_TEST_DB_USER" "$DB_ROLE" "export "
  upsert_env_value "$ROOT_DIR/deploy/bashrc" "VH_TEST_DB_PASS" "$VAUL_TEST_PG_PASS" "export "
  upsert_env_value "$ROOT_DIR/deploy/bashrc" "VH_TEST_DB_HOST" "127.0.0.1" "export "
  upsert_env_value "$ROOT_DIR/deploy/bashrc" "VH_TEST_DB_PORT" "5432" "export "
  upsert_env_value "$ROOT_DIR/deploy/bashrc" "VH_TEST_DB_NAME" "$DB_NAME" "export "

  upsert_env_value "$ROOT_DIR/deploy/vaulthalla.env" "VH_TEST_DB_USER" "$DB_ROLE"
  upsert_env_value "$ROOT_DIR/deploy/vaulthalla.env" "VH_TEST_DB_PASS" "$VAUL_TEST_PG_PASS"
  upsert_env_value "$ROOT_DIR/deploy/vaulthalla.env" "VH_TEST_DB_HOST" "127.0.0.1"
  upsert_env_value "$ROOT_DIR/deploy/vaulthalla.env" "VH_TEST_DB_PORT" "5432"
  upsert_env_value "$ROOT_DIR/deploy/vaulthalla.env" "VH_TEST_DB_NAME" "$DB_NAME"

  export VH_TEST_DB_PASS="${VAUL_TEST_PG_PASS}"
fi
