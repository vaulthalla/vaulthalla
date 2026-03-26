#!/usr/bin/env bash
set -euo pipefail

# ═══════════════════════════════════════════════════════
#             ⚔️  VAULTHALLA INSTALLATION ⚔️
#       This script sets up the entire environment
# ═══════════════════════════════════════════════════════

source ./bin/lib/dev_mode.sh

echo "🗡️  Initiating Vaulthalla uninstallation sequence..."

vh_assert_dev_mode_consistency

DEV_MODE=false
if vh_is_dev_mode; then
    DEV_MODE=true
fi

echo "🔍 Build mode: ${VH_BUILD_MODE:-unset}"
echo "🔍 Dev mode active: $DEV_MODE"

BUILD_MANPAGE=false
CLEAN_BUILD=false

# parse command line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)
          CLEAN_BUILD=true
          shift
          ;;
        -m|--manpage)
            BUILD_MANPAGE=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo "  -d, --dev        Enable dev mode (e.g., debug build)"
            echo "  -m, --manpage    Build and install manpage"
            echo "  -h, --help       Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help to see available options."
            exit 1
            ;;
    esac
done

# Construct Meson setup args
MESON_ARGS=()

if [[ "$DEV_MODE" == true ]]; then
    MESON_ARGS+=("-Dbuildtype=debug")
else
    MESON_ARGS+=("-Dbuildtype=release")
fi

if [[ "$BUILD_MANPAGE" == true ]]; then
    MESON_ARGS+=("-Dmanpage=true")
else
    MESON_ARGS+=("-Dmanpage=false")
fi

echo "Setting up build with:"
printf '  %s\n' "${MESON_ARGS[@]}"

# Check if we're root OR have passwordless sudo
if [[ $EUID -ne 0 ]]; then
    if ! sudo -n true 2>/dev/null; then
        echo "❗️ This script must be run as root or with passwordless sudo."
        exit 1
    fi
fi

# Clean build environment
if [[ -d build && "$CLEAN_BUILD" == true ]]; then
    echo "🧹 Cleaning previous build artifacts..."
    rm -rf build
else
    mkdir -p build
fi

# === 1) Install Build Dependencies ===
./bin/setup/install_deps.sh

# === 2) Create System User and Group ===
./bin/setup/install_users.sh

# === 3) Build Project ===
echo "🏗️  Starting Vaulthalla build..."

meson setup build "${MESON_ARGS[@]}" -Db_sanitize=address,undefined
meson compile -C build
sudo meson install -C build
sudo ldconfig

# === 4) Setup Database ===
./bin/setup/install_db.sh

# === 5) Install systemd service ===
./bin/setup/install_systemd.sh
