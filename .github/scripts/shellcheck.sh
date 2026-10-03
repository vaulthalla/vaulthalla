#!/usr/bin/env bash
# Shell lint for maintainer scripts and repo shell helpers.
#
# Tiers:
#   blocking  debian/postinst, debian/prerm, debian/postrm (severity >= warning). These run as root
#             under dpkg on every install/upgrade; they were clean when this gate landed, keep them so.
#   advisory  bin/**/*.sh (all severities). Pre-existing findings in dev helper scripts; reported and
#             counted but not failing, until they are cleaned up. Set SHELLCHECK_ADVISORY_BLOCKING=1 to
#             make this tier fail too.
#
# Usage: .github/scripts/shellcheck.sh   (from the repo root)
set -uo pipefail

if ! command -v shellcheck >/dev/null 2>&1; then
  echo "::error::shellcheck is not installed (apt-get install -y shellcheck)."
  exit 2
fi
shellcheck --version | sed -n '2p'

blocking=(debian/postinst debian/prerm debian/postrm)
mapfile -t advisory < <(find bin -type f -name '*.sh' | LC_ALL=C sort)

annotate() {
  # gcc format: file:line:col: severity: message [SCxxxx]
  local level="$1"
  while IFS= read -r line; do
    [ -n "$line" ] || continue
    file="${line%%:*}"; rest="${line#*:}"; lineno="${rest%%:*}"
    echo "::${level} file=${file},line=${lineno}::${line}"
  done
}

blocking_out="$(shellcheck -f gcc -S warning "${blocking[@]}" 2>&1)"
blocking_count=0
[ -n "$blocking_out" ] && blocking_count="$(printf '%s\n' "$blocking_out" | grep -c ': \(error\|warning\):' || true)"
printf '%s\n' "$blocking_out" | annotate error

advisory_out="$(shellcheck -f gcc "${advisory[@]}" 2>&1)"
advisory_count=0
[ -n "$advisory_out" ] && advisory_count="$(printf '%s\n' "$advisory_out" | grep -c ': \(error\|warning\|note\|style\):' || true)"
printf '%s\n' "$advisory_out" | annotate warning

summary="shellcheck: maintainer scripts (blocking, >=warning): ${blocking_count} finding(s); bin/**/*.sh (${#advisory[@]} files, advisory): ${advisory_count} finding(s)"
echo "$summary"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
  {
    echo "### shellcheck"
    echo ""
    echo "- maintainer scripts (blocking, severity >= warning): **${blocking_count}**"
    echo "- \`bin/**/*.sh\` (${#advisory[@]} files, advisory): **${advisory_count}**"
  } >> "$GITHUB_STEP_SUMMARY"
fi

status=0
if [ "$blocking_count" -gt 0 ]; then
  echo "::error::shellcheck found ${blocking_count} finding(s) in Debian maintainer scripts."
  status=1
fi
if [ "${SHELLCHECK_ADVISORY_BLOCKING:-0}" = "1" ] && [ "$advisory_count" -gt 0 ]; then
  status=1
fi
exit "$status"
