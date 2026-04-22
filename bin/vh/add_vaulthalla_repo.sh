#!/usr/bin/env bash
set -euo pipefail

readonly KEY_URL="${VH_APT_KEY_URL:-https://apt.vaulthalla.sh/pubkey.gpg}"
readonly REPO_URL="${VH_APT_REPO_URL:-https://apt.vaulthalla.sh}"
readonly REPO_DIST="${VH_APT_DIST:-stable}"
readonly REPO_COMPONENT="${VH_APT_COMPONENT:-main}"
readonly REPO_ARCH="${VH_APT_ARCH:-$(dpkg --print-architecture 2>/dev/null || echo amd64)}"

readonly KEY_FILE="/etc/apt/trusted.gpg.d/vaulthalla.gpg"
readonly SOURCE_FILE="/etc/apt/sources.list.d/vaulthalla.list"
readonly SOURCE_LINE="deb [arch=${REPO_ARCH}] ${REPO_URL} ${REPO_DIST} ${REPO_COMPONENT}"

log() { printf '[vh add-repo] %s\n' "$*"; }
die() { printf '[vh add-repo] ERROR: %s\n' "$*" >&2; exit 1; }

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

ensure_supported_environment() {
    [[ "$(uname -s)" == "Linux" ]] || die "Unsupported OS: $(uname -s). This helper supports Debian/Ubuntu Linux only."
    [[ -r /etc/os-release ]] || die "Missing /etc/os-release. Cannot determine distribution."
    [[ -r /etc/debian_version ]] || die "Unsupported distro: /etc/debian_version is missing."

    # shellcheck source=/dev/null
    . /etc/os-release

    local id="${ID:-unknown}"
    local id_like="${ID_LIKE:-}"
    case "$id" in
        debian|ubuntu) ;;
        *)
            if [[ "$id_like" != *debian* ]]; then
                die "Unsupported distro: ${id}. This helper only supports Debian/Ubuntu apt environments."
            fi
            ;;
    esac

    need_cmd apt-get
    need_cmd curl
    need_cmd install
    need_cmd cmp
    need_cmd tee
}

main() {
    ensure_supported_environment

    local tmp_key
    tmp_key="$(mktemp)"
    trap 'rm -f "$tmp_key"' EXIT

    log "Downloading Vaulthalla repository key from ${KEY_URL}"
    curl -fsSL "$KEY_URL" -o "$tmp_key"
    [[ -s "$tmp_key" ]] || die "Downloaded key is empty: ${KEY_URL}"

    run_priv install -d -m 0755 /etc/apt/trusted.gpg.d /etc/apt/sources.list.d

    local key_status
    if [[ -f "$KEY_FILE" ]] && cmp -s "$tmp_key" "$KEY_FILE"; then
        key_status="already configured"
        log "Repository key is already configured: ${KEY_FILE}"
    else
        run_priv install -m 0644 -o root -g root "$tmp_key" "$KEY_FILE"
        key_status="updated"
        log "Repository key installed: ${KEY_FILE}"
    fi

    local source_status
    local expected_content
    expected_content="$(printf '%s\n' "$SOURCE_LINE")"
    if [[ -f "$SOURCE_FILE" ]] && [[ "$(cat "$SOURCE_FILE")"$'\n' == "$expected_content" ]]; then
        source_status="already configured"
        log "Repository source is already configured: ${SOURCE_FILE}"
    else
        printf '%s\n' "$SOURCE_LINE" | run_priv tee "$SOURCE_FILE" >/dev/null
        run_priv chmod 0644 "$SOURCE_FILE"
        source_status="updated"
        log "Repository source installed: ${SOURCE_FILE}"
    fi

    log "Summary: key=${key_status}, source=${source_status}"
}

main "$@"
