#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

VH_SKIP_NGINX_CONFIG=1 exec "${SCRIPT_DIR}/install.sh" "$@"
