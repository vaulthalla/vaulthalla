#!/usr/bin/env bash
set -euo pipefail

readonly VH_GROUP="vaulthalla"
readonly ASSIGN_ADMIN_CMD="vh setup assign-admin"

log() { printf '[vh init] %s\n' "$*"; }
warn() { printf '[vh init] WARN: %s\n' "$*" >&2; }
die() { printf '[vh init] ERROR: %s\n' "$*" >&2; exit 1; }

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

configured_in_group() {
    local user="$1"
    id -nG "$user" 2>/dev/null | tr ' ' '\n' | grep -Fxq "$VH_GROUP"
}

current_shell_in_group() {
    id -nG 2>/dev/null | tr ' ' '\n' | grep -Fxq "$VH_GROUP"
}

resolve_operator_user() {
    if [[ -n "${VH_OPERATOR_USER:-}" ]]; then
        printf '%s\n' "$VH_OPERATOR_USER"
        return
    fi

    if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != "root" ]]; then
        printf '%s\n' "$SUDO_USER"
        return
    fi

    id -un
}

main() {
    local operator_user
    operator_user="$(resolve_operator_user)"
    [[ -n "$operator_user" ]] || die "Unable to determine operator user. Set VH_OPERATOR_USER and rerun."

    id "$operator_user" >/dev/null 2>&1 || die "Operator user does not exist: ${operator_user}"
    getent group "$VH_GROUP" >/dev/null 2>&1 || die "Group '${VH_GROUP}' does not exist. Verify package installation."

    local group_status
    if [[ "$operator_user" == "root" ]]; then
        group_status="skipped (operator user is root)"
    elif configured_in_group "$operator_user"; then
        group_status="already enrolled"
    else
        run_priv usermod -aG "$VH_GROUP" "$operator_user"
        group_status="added ${operator_user} to ${VH_GROUP}"
    fi

    local invoking_user
    invoking_user="$(id -un)"

    local shell_has_group="unknown"
    if [[ "$invoking_user" == "$operator_user" ]]; then
        if current_shell_in_group; then
            shell_has_group="yes"
        else
            shell_has_group="no"
        fi
    fi

    local session_status
    local session_next_step=""
    if [[ "$invoking_user" != "$operator_user" ]]; then
        session_status="not checked (current shell user '${invoking_user}' differs from operator '${operator_user}')"
    elif [[ "$shell_has_group" == "yes" ]]; then
        session_status="current shell already has '${VH_GROUP}' group access"
    else
        session_status="current shell may not yet have '${VH_GROUP}' group access"
        if command -v newgrp >/dev/null 2>&1; then
            session_next_step="newgrp ${VH_GROUP}"
        else
            session_next_step="log out and log back in"
        fi
    fi

    local assign_status="not attempted"
    local assign_hint="${ASSIGN_ADMIN_CMD}"
    local assign_detail=""

    if ! command -v vh >/dev/null 2>&1; then
        assign_status="pending (vh CLI not found in PATH)"
    elif [[ "$operator_user" == "root" ]]; then
        assign_status="pending (operator user is root; run claim from a non-root operator shell)"
    elif [[ "$invoking_user" != "$operator_user" ]]; then
        assign_status="pending (run as operator user '${operator_user}')"
    else
        if [[ "$shell_has_group" == "yes" ]]; then
            if assign_detail="$(vh setup assign-admin 2>&1)"; then
                assign_status="completed (direct)"
            else
                assign_status="pending (direct command failed)"
            fi
        elif command -v sg >/dev/null 2>&1; then
            if assign_detail="$(sg "$VH_GROUP" -c "vh setup assign-admin" 2>&1)"; then
                assign_status="completed (via sg one-shot shell)"
            else
                assign_status="pending (sg one-shot command failed)"
            fi
        else
            assign_status="pending (current shell lacks '${VH_GROUP}' and 'sg' is unavailable)"
        fi
    fi

    if [[ "$assign_status" == pending* ]] && [[ "$assign_detail" == *"Unknown setup subcommand"* ]]; then
        assign_status="pending (this build does not provide 'vh setup assign-admin' yet)"
        assign_hint="Run 'vh' once for first-use claim, then rerun setup commands."
    fi

    local assign_first_line=""
    if [[ -n "$assign_detail" ]]; then
        assign_first_line="$(printf '%s\n' "$assign_detail" | head -n 1)"
    fi

    log "Post-install summary:"
    log "  operator user: ${operator_user}"
    log "  group enrollment: ${group_status}"
    log "  session/group refresh: ${session_status}"
    if [[ -n "$session_next_step" ]]; then
        log "  session next step: run '${session_next_step}'"
        log "  note: a full new login shell/session may still be required for parent-shell group refresh."
    fi
    log "  admin assignment: ${assign_status}"
    if [[ -n "$assign_first_line" ]]; then
        log "  admin assignment detail: ${assign_first_line}"
    fi

    log "Recommended next commands:"
    log "  ${ASSIGN_ADMIN_CMD}"
    log "  vh setup db"
    log "  vh setup remote-db"
    log "  vh setup nginx"
    log "  vh setup nginx --certbot --domain <domain>"

    if [[ "$assign_status" == pending* ]]; then
        warn "Admin assignment is still pending."
        warn "Next step: ${assign_hint}"
    fi
}

main "$@"
