source "$(dirname "$0")/../lib/dev_mode.sh"

vh_require_dev_mode

if vh_is_dev_mode; then
  VAUL_TEST_PG_PASS=$(openssl rand -base64 32 | tr -d '\n')

  sudo -u postgres psql -tc "SELECT 1 FROM pg_roles WHERE rolname='vaulthalla_test'" | grep -q 1 ||
    sudo -u postgres psql -c "CREATE ROLE vaulthalla_test WITH LOGIN PASSWORD '${VAUL_TEST_PG_PASS}';"

  sudo -u postgres psql -tc "SELECT 1 FROM pg_database WHERE datname='vh_cli_test'" | grep -q 1 ||
    sudo -u postgres psql -c "CREATE DATABASE vh_cli_test OWNER vaulthalla_test;"

  sudo -u postgres psql -c "GRANT ALL PRIVILEGES ON DATABASE vh_cli_test TO vaulthalla_test;"
  sudo -u postgres psql -d vh_cli_test -c "GRANT USAGE, CREATE ON SCHEMA public TO vaulthalla_test;"

  # 🔥 Persist to bashrc (replace if exists, append if not)
  if grep -q "^export VH_TEST_DB_PASS=" "./deploy/vaulthalla.env"; then
    sed -i "s|^export VH_TEST_DB_PASS=.*|export VH_TEST_DB_PASS=\"${VAUL_TEST_PG_PASS}\"|" "./deploy/vaulthalla.env"
  else
    echo "export VH_TEST_DB_PASS=\"${VAUL_TEST_PG_PASS}\"" >> "./deploy/vaulthalla.env"
  fi

  # 🔥 Export for current session
  export VH_TEST_DB_PASS="${VAUL_TEST_PG_PASS}"
fi