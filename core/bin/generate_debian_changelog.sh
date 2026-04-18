#!/usr/bin/env bash
set -euo pipefail

PKG_NAME="vaulthalla"
EMAIL="cooper@valkyrianlabs.com"
NAME="Cooper Larson"
NOTES_FILE=".release_notes"

VERSION="$(meson introspect --projectinfo build 2>/dev/null \
          | sed -En 's/.*\"version\": *\"([^\"]+)\".*/\1/p')"
DEB_VERSION="${VERSION}-1"
DATE="$(date -R)"

[[ -s "$NOTES_FILE" ]] || { echo "No changelog notes found."; exit 1; }

{
  echo "$PKG_NAME ($DEB_VERSION) unstable; urgency=medium"
  echo
  cat "$NOTES_FILE"
  echo
  echo " -- $NAME <$EMAIL>  $DATE"
} > debian/changelog
