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

CLEAN_BUILD=false
RUN_TEST=false

# parse command line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --run)
          RUN_TEST=true
          shift
          ;;
        --clean)
          CLEAN_BUILD=true
          shift
          ;;
        -h|--help)
            echo "Usage: $0 [options]"
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
MESON_ARGS+=("-Dbuildtype=debug")
MESON_ARGS+=("-Dintegration_tests=true")

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

# === 3) Install Directories ===
./bin/tests/install_dirs.sh

# === 4) Setup Database ===
./bin/tests/install_db.sh

# === 5) Run Tests ===
if [[ "$RUN_TEST" == true ]]; then
  echo "🏗️  Starting Vaulthalla build..."

  meson setup build "${MESON_ARGS[@]}" -Db_sanitize=address,undefined
  meson compile -C build
  sudo meson install -C build
  sudo ldconfig

  sudo bash --rcfile './deploy/vaulthalla.env' -i -c "./build/vh_integration_tests"
fi
