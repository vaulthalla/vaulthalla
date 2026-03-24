#!/usr/bin/env bash
set -euo pipefail

# === 1) Create 'vaulthalla' group if it doesn't exist ===
if ! getent group vaulthalla > /dev/null; then
    echo "👥 Creating system group 'vaulthalla'..."
    sudo groupadd --system vaulthalla
else
    echo "👥 System group 'vaulthalla' already exists."
fi

# === 2) Create 'vaulthalla' user if it doesn't exist ===
if ! id -u vaulthalla >/dev/null 2>&1; then
    echo "👤 Creating system user 'vaulthalla'..."
    sudo useradd \
        --system \
        --gid vaulthalla \
        --shell /usr/sbin/nologin \
        --no-create-home \
        vaulthalla
else
    echo "👤 System user 'vaulthalla' already exists."
fi

# === 3) Add invoking user to 'vaulthalla' group ===
REAL_USER="${SUDO_USER:-${USER:-}}"

if [[ -z "$REAL_USER" || "$REAL_USER" == "root" ]]; then
    # fallback if script wasn't run via sudo
    REAL_USER="$(logname 2>/dev/null || true)"
fi

if [[ -n "$REAL_USER" && "$REAL_USER" != "root" ]]; then
    echo "👤 Adding user '$REAL_USER' to 'vaulthalla' group..."
    sudo usermod -aG vaulthalla "$REAL_USER"
    echo "ℹ️ Log out and back in for group changes to take effect."
else
    echo "⚠️ Could not determine a non-root invoking user; skipping group addition."
fi

# === 4) Ensure 'tss' group exists and add 'vaulthalla' user ===
if ! getent group tss > /dev/null; then
    echo "👥 Creating system group 'tss'..."
    sudo groupadd --system tss
fi

echo "🔑 Adding 'vaulthalla' user to 'tss' group..."
sudo usermod -aG tss vaulthalla

# === 5) Ensure TPM device permissions ===
udev_rule_file="/etc/udev/rules.d/60-tpm.rules"

check_perms() {
    local dev=$1
    if [[ -e "$dev" ]]; then
        local perms
        perms=$(stat -c "%a %G" "$dev")
        [[ "$perms" == "660 tss" ]]
    else
        return 0
    fi
}

if ! check_perms /dev/tpm0 || ! check_perms /dev/tpmrm0; then
    echo "🔧 Fixing TPM device permissions via udev rule..."
    cat <<'EOF' | sudo tee "$udev_rule_file" >/dev/null
KERNEL=="tpm[0-9]*",   GROUP="tss", MODE="0660"
KERNEL=="tpmrm[0-9]*", GROUP="tss", MODE="0660"
EOF
    sudo udevadm control --reload-rules
    sudo udevadm trigger
else
    echo "✅ TPM device permissions already correct."
fi
