echo "🔐 Creating PostgreSQL user and database..."

ROLE_CREATED=false
if sudo -u postgres psql -tc "SELECT 1 FROM pg_roles WHERE rolname='vaulthalla'" | grep -q 1; then
  echo "ℹ️ PostgreSQL role 'vaulthalla' already exists."
else
  VAUL_PG_PASS=$(openssl rand -base64 32 | tr -d '\n')
  sudo -u postgres psql -c "CREATE USER vaulthalla WITH PASSWORD '${VAUL_PG_PASS}';"
  ROLE_CREATED=true
  echo "✅ Created PostgreSQL role 'vaulthalla'."
fi

sudo -u postgres psql -tc "SELECT 1 FROM pg_database WHERE datname='vaulthalla'" | grep -q 1 ||
  sudo -u postgres psql -c "CREATE DATABASE vaulthalla OWNER vaulthalla;"

sudo -u postgres psql -c "GRANT ALL PRIVILEGES ON DATABASE vaulthalla TO vaulthalla;"
sudo -u postgres psql -d vaulthalla -c "GRANT USAGE, CREATE ON SCHEMA public TO vaulthalla;"

# === Seed DB Password (for runtime TPM sealing) ===
if [[ "$ROLE_CREATED" == "true" ]]; then
  PENDING_DB_PASS_FILE="/run/vaulthalla/db_password"
  echo "🔐 Seeding PostgreSQL password for Vaulthalla user (pending runtime init)..."
  echo "$VAUL_PG_PASS" | sudo tee "$PENDING_DB_PASS_FILE" >/dev/null
  sudo chown vaulthalla:vaulthalla "$PENDING_DB_PASS_FILE"
  sudo chmod 600 "$PENDING_DB_PASS_FILE"
  echo "✅ Wrote DB password seed to $PENDING_DB_PASS_FILE"
else
  echo "ℹ️ Role existed already; leaving DB password seed state unchanged."
fi
