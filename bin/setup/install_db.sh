#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

source "$ROOT_DIR/bin/lib/dev_mode.sh"

echo "🔐 Creating PostgreSQL user and database..."

DB_NAME="vaulthalla"
DB_ROLE="vaulthalla"
PENDING_DB_PASS_FILE="/run/vaulthalla/db_password"

DEV_MODE=false
vh_is_dev_mode && DEV_MODE=true

ROLE_CREATED=false
PASSWORD_READY=false
VAUL_PG_PASS="$(openssl rand -base64 32 | tr -d '\n')"

if sudo -u postgres psql -v ON_ERROR_STOP=1 -d postgres -tc "SELECT 1 FROM pg_roles WHERE rolname='${DB_ROLE}'" | grep -q 1; then
  echo "ℹ️ PostgreSQL role '${DB_ROLE}' already exists."
  if [[ "$DEV_MODE" == true ]]; then
    sudo -u postgres psql -v ON_ERROR_STOP=1 -d postgres -c "ALTER ROLE ${DB_ROLE} WITH LOGIN PASSWORD '${VAUL_PG_PASS}';"
    PASSWORD_READY=true
    echo "✅ Reset PostgreSQL password for existing dev role '${DB_ROLE}'."
  fi
else
  sudo -u postgres psql -v ON_ERROR_STOP=1 -d postgres -c "CREATE USER ${DB_ROLE} WITH PASSWORD '${VAUL_PG_PASS}';"
  ROLE_CREATED=true
  PASSWORD_READY=true
  echo "✅ Created PostgreSQL role '${DB_ROLE}'."
fi

if sudo -u postgres psql -v ON_ERROR_STOP=1 -d postgres -tc "SELECT 1 FROM pg_database WHERE datname='${DB_NAME}'" | grep -q 1; then
  sudo -u postgres psql -v ON_ERROR_STOP=1 -d postgres -c "ALTER DATABASE ${DB_NAME} OWNER TO ${DB_ROLE};"
else
  sudo -u postgres psql -v ON_ERROR_STOP=1 -d postgres -c "CREATE DATABASE ${DB_NAME} OWNER ${DB_ROLE};"
fi

sudo -u postgres psql -v ON_ERROR_STOP=1 -d postgres -c "GRANT ALL PRIVILEGES ON DATABASE ${DB_NAME} TO ${DB_ROLE};"
sudo -u postgres psql -v ON_ERROR_STOP=1 -d "${DB_NAME}" -c "GRANT USAGE, CREATE ON SCHEMA public TO ${DB_ROLE};"

# === Seed DB Password (for runtime TPM sealing) ===
if [[ "$PASSWORD_READY" == true ]]; then
  echo "🔐 Seeding PostgreSQL password for Vaulthalla user (pending runtime init)..."
  sudo install -d -m 0755 "$(dirname "$PENDING_DB_PASS_FILE")"
  echo "$VAUL_PG_PASS" | sudo tee "$PENDING_DB_PASS_FILE" >/dev/null
  sudo chown vaulthalla:vaulthalla "$PENDING_DB_PASS_FILE"
  sudo chmod 600 "$PENDING_DB_PASS_FILE"
  echo "✅ Wrote DB password seed to $PENDING_DB_PASS_FILE"
elif [[ "$ROLE_CREATED" == false ]]; then
  echo "ℹ️ Role existed already outside dev mode; leaving DB password seed state unchanged."
fi
