#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/../lib/dev_mode.sh"

vh_require_dev_mode

DB_NAME="vaulthalla"
DB_ROLE="vaulthalla"

if ! command -v psql >/dev/null 2>&1 || ! id -u postgres >/dev/null 2>&1; then
    echo "🛡️  PostgreSQL client not installed or 'postgres' user missing. Skipping database cleanup."
    exit 0
fi

pg_server_ready() {
    if command -v pg_isready >/dev/null 2>&1; then
        sudo -u postgres pg_isready -q
        return $?
    fi

    sudo -u postgres psql -d postgres -Atqc 'SELECT 1;' >/dev/null 2>&1
}

if ! pg_server_ready; then
    echo "🛡️  PostgreSQL server is not accepting local connections. Skipping database cleanup."
    exit 0
fi

if vh_confirm "⚠️  Drop PostgreSQL database and user '$DB_ROLE'?" "N" "Y"; then
    if vh_is_dev_mode; then
        echo "⚠️  [DEV_MODE] Dropping PostgreSQL DB and user '$DB_ROLE'..."
    else
        echo "🧨 Dropping PostgreSQL DB and user..."
    fi

    sudo -u postgres psql -d postgres <<EOF
REVOKE CONNECT ON DATABASE $DB_NAME FROM public;
SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE datname = '$DB_NAME';
DROP DATABASE IF EXISTS $DB_NAME;
DROP ROLE IF EXISTS $DB_ROLE;
EOF
else
    echo "🛡️  Skipping database deletion. You can do it manually with psql if needed."
fi
