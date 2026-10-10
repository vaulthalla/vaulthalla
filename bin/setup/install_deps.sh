#!/bin/bash

set -euo pipefail

echo "🔍 Checking for required build dependencies..."

# apt.vaulthalla.sh is the only source of Vaulthalla's own packages and build SDKs (libpdfium-dev,
# libpqxx-vh-dev). Same keyring and source line as bin/vh/install.sh.
readonly VH_KEYRING="/usr/share/keyrings/vaulthalla.gpg"
readonly VH_SOURCE_FILE="/etc/apt/sources.list.d/vaulthalla.list"
VH_ARCH="$(dpkg --print-architecture)"
readonly VH_ARCH
readonly VH_SOURCE_LINE="deb [arch=${VH_ARCH} signed-by=${VH_KEYRING}] https://apt.vaulthalla.sh stable main"

echo "🔗 Installing Vaulthalla public key..."
curl -fsSL https://apt.vaulthalla.sh/pubkey.gpg | sudo install -D -m 0644 /dev/stdin "$VH_KEYRING"

if [ "$(cat "$VH_SOURCE_FILE" 2>/dev/null)" != "$VH_SOURCE_LINE" ]; then
    echo "🔗 Configuring the Vaulthalla repository..."
    echo "$VH_SOURCE_LINE" | sudo tee "$VH_SOURCE_FILE" > /dev/null
    # The old setup trusted the key globally; the source line now names its keyring.
    sudo rm -f /etc/apt/trusted.gpg.d/vaulthalla.gpg
else
    echo "✅ Vaulthalla repository already configured."
fi

# Wait for other apt/dpkg users (e.g. unattended-upgrades after a reboot) instead of failing after 120s.
APT_LOCK_OPTS=(-o DPkg::Lock::Timeout=900)

sudo apt-get "${APT_LOCK_OPTS[@]}" update

check_pkg() {
    local pkg="$1"
    local desc="$2"
    if ! dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "ok installed"; then
        echo "🔌 Installing $desc..."
        sudo apt-get "${APT_LOCK_OPTS[@]}" install -y "$pkg"
    else
        echo "✅ $desc already installed."
    fi
}

# -- Build tools --
if ! command -v meson &>/dev/null || ! command -v ninja &>/dev/null; then
    echo "🛠️ Installing Meson and Ninja build system..."
    sudo apt-get "${APT_LOCK_OPTS[@]}" install -y meson ninja-build
else
    echo "✅ Meson and Ninja already installed."
fi

# -- Vaulthalla maintained packages (apt.vaulthalla.sh) --
# libpdfium-dev moved from date versions (20250629) to Chromium ones (155.8059.x), which dpkg orders lower, so
# apt never replaces the retired snapshot by itself: install the repository's version explicitly.
pdfium_repo_version="$(apt-cache madison libpdfium-dev | awk -F'|' '/apt\.vaulthalla\.sh/ {gsub(/ /, "", $2); print $2; exit}')"
if [[ "$(dpkg-query -W -f='${Version}' libpdfium-dev 2>/dev/null)" != "$pdfium_repo_version" ]]; then
    echo "🔌 Installing libpdfium-dev ${pdfium_repo_version}..."
    sudo apt-get "${APT_LOCK_OPTS[@]}" install -y --allow-downgrades "libpdfium-dev=${pdfium_repo_version}"
else
    echo "✅ libpdfium-dev ${pdfium_repo_version} already installed."
fi
check_pkg libpqxx-vh-dev "libpqxx-vh-dev (static libpqxx SDK)"

# -- Libraries --
check_pkg postgresql "postgresql"
check_pkg nginx "nginx"
check_pkg certbot "certbot"
check_pkg python3-certbot-nginx "python3-certbot-nginx"
check_pkg python3-certbot-dns-cloudflare "python3-certbot-dns-cloudflare"
check_pkg pkg-config "pkg-config"
check_pkg libturbojpeg0-dev "libturbojpeg0-dev"
check_pkg libmagic1 "libmagic1"
check_pkg libmagic-dev "libmagic-dev"
check_pkg libsodium-dev "libsodium-dev"
check_pkg libcurl4-openssl-dev "libcurl4-openssl-dev"
check_pkg uuid-dev "uuid-dev"
check_pkg libfuse3-dev "libfuse3-dev"
check_pkg libyaml-cpp-dev "libyaml-cpp-dev"
check_pkg libpugixml-dev "libpugixml-dev"
check_pkg libgtest-dev "libgtest-dev"
check_pkg libboost-filesystem-dev "Boost filesystem"
check_pkg libboost-system-dev "Boost system"
check_pkg libspdlog-dev "libspdlog-dev"
check_pkg libtss2-dev "libtss2-dev"
check_pkg tpm2-tools "tpm2-tools"
check_pkg swtpm "swtpm"
check_pkg swtpm-tools "swtpm-tools"
