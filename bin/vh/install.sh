#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly ADD_REPO_SCRIPT="${SCRIPT_DIR}/add_vaulthalla_repo.sh"
readonly INIT_SCRIPT="${SCRIPT_DIR}/init_cli.sh"

log() { printf '[vh install] %s\n' "$*"; }
die() { printf '[vh install] ERROR: %s\n' "$*" >&2; exit 1; }

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "Missing required command: $1"
}

run_priv() {
    if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
        "$@"
        return
    fi

    need_cmd sudo
    sudo "$@"
}

usage() {
    cat <<'EOF'
Usage: ./install.sh [apt-install-options]

Installs Vaulthalla via apt using this flow:
1) add Vaulthalla apt repo/key
2) apt-get update
3) apt-get install vaulthalla
4) post-install CLI onboarding (group + admin-claim flow)

Examples:
  ./install.sh
  ./install.sh --no-install-recommends
  VH_SKIP_DB_BOOTSTRAP=1 ./install.sh
  VH_SKIP_NGINX_CONFIG=1 ./install.sh
EOF
}

main() {
    if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
        usage
        return 0
    fi

    [[ -x "$ADD_REPO_SCRIPT" ]] || die "Missing executable helper: ${ADD_REPO_SCRIPT}"
    [[ -x "$INIT_SCRIPT" ]] || die "Missing executable helper: ${INIT_SCRIPT}"

    log "Bootstrapping Vaulthalla apt repository."
    "$ADD_REPO_SCRIPT"

    log "Running apt-get update."
    run_priv apt-get update

    local -a env_args=()
    [[ -n "${VH_SKIP_DB_BOOTSTRAP:-}" ]] && env_args+=("VH_SKIP_DB_BOOTSTRAP=${VH_SKIP_DB_BOOTSTRAP}")
    [[ -n "${VH_SKIP_NGINX_CONFIG:-}" ]] && env_args+=("VH_SKIP_NGINX_CONFIG=${VH_SKIP_NGINX_CONFIG}")

    local -a install_cmd=(apt-get install -y "$@" vaulthalla)
    log "Installing Vaulthalla package."
    if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
        if [[ ${#env_args[@]} -gt 0 ]]; then
            env "${env_args[@]}" "${install_cmd[@]}"
        else
            "${install_cmd[@]}"
        fi
    else
        need_cmd sudo
        if [[ ${#env_args[@]} -gt 0 ]]; then
            sudo env "${env_args[@]}" "${install_cmd[@]}"
        else
            sudo "${install_cmd[@]}"
        fi
    fi

    log "Running post-install CLI onboarding."
    "$INIT_SCRIPT"
}

main "$@"
