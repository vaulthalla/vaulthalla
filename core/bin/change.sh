#!/usr/bin/env bash
set -euo pipefail

NOTES_FILE=".release_notes"

[[ $# -eq 0 ]] && { echo "Usage: $0 \"Change description\""; exit 1; }

echo "  * $1" >> "$NOTES_FILE"
echo "Added: $1"
