#!/usr/bin/env bash

vh_project_root() {
    cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd
}

vh_dev_sentinel_path() {
    echo "$(vh_project_root)/enable_dev_mode"
}

vh_is_dev_mode() {
    local sentinel
    sentinel="$(vh_dev_sentinel_path)"

    [[ "${VH_BUILD_MODE:-}" == "dev" ]] &&
    [[ -f "$sentinel" ]] &&
    [[ "$(tr -d '[:space:]' < "$sentinel")" == "true" ]]
}

vh_require_dev_mode() {
    if ! vh_is_dev_mode; then
        echo "❌ This operation requires dev mode (VH_BUILD_MODE=dev + enable_dev_mode sentinel)."
        exit 1
    fi
}

vh_assert_dev_mode_consistency() {
    local sentinel
    sentinel="$(vh_dev_sentinel_path)"

    if [[ "${VH_BUILD_MODE:-}" == "dev" && ! -f "$sentinel" ]]; then
        echo "❌ VH_BUILD_MODE=dev but sentinel missing: $sentinel"
        exit 1
    fi

    if [[ -f "$sentinel" ]] && [[ "${VH_BUILD_MODE:-}" != "dev" ]]; then
        echo "⚠️ Dev sentinel present but VH_BUILD_MODE != dev."
    fi
}

vh_confirm() {
    local prompt="$1"
    local default="${2:-N}"
    local dev_default="${3:-}"

    if vh_is_dev_mode && [[ -n "$dev_default" ]]; then
        case "$dev_default" in
            Y|y)
                echo "$prompt [auto-yes: dev mode]"
                return 0
                ;;
            N|n)
                echo "$prompt [auto-no: dev mode]"
                return 1
                ;;
            *)
                echo "❌ vh_confirm: invalid dev default '$dev_default'"
                exit 1
                ;;
        esac
    fi

    local reply
    if [[ "$default" == "Y" ]]; then
        read -r -p "$prompt [Y/n]: " reply
        [[ -z "$reply" || "$reply" =~ ^[Yy]$ ]]
    else
        read -r -p "$prompt [y/N]: " reply
        [[ "$reply" =~ ^[Yy]$ ]]
    fi
}
