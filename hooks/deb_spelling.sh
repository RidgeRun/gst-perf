#!/usr/bin/env bash

set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
command -v npx >/dev/null 2>&1 || {
  echo "npx is required to run the Debian file spellcheck" >&2
  exit 1
}

spellcheck_dir="$(mktemp -d "${PWD}/debian-spelling.XXXXXX")"
trap 'rm -rf "${spellcheck_dir}"' EXIT

# Check changelog prose, without checking versions, dates, or maintainer data.
awk '
  /^[[:space:]]*\*/ {
    print
    in_entry = 1
    next
  }
  /^ -- / { in_entry = 0; print ""; next }
  in_entry && /^[[:space:]]{3,}/ {
    print
    next
  }
  { print "" }
' debian/changelog >"${spellcheck_dir}/changelog.txt"

# Check only the package description and its continuation lines.
awk '
  /^Description:/ {
    print
    in_description = 1
    next
  }
  in_description && /^[[:space:]]/ {
    print
    next
  }
  { in_description = 0; print "" }
' debian/control >"${spellcheck_dir}/control-description.txt"

# Debian rules has no user-facing fields; check only its comments.
awk '/^[[:space:]]*#/ { print; next } { print "" }' \
  debian/rules >"${spellcheck_dir}/rules-comments.txt"

spellcheck_output="$(mktemp)"
trap 'rm -f "${spellcheck_output}"; rm -rf "${spellcheck_dir}"' EXIT
spellcheck_status=0
npx --no-install cspell --no-progress \
  "${spellcheck_dir}/changelog.txt" \
  "${spellcheck_dir}/control-description.txt" \
  "${spellcheck_dir}/rules-comments.txt" >"${spellcheck_output}" 2>&1 || \
  spellcheck_status=$?

sed -E \
  -e 's|[^:[:space:]]+/changelog\.txt:|debian/changelog:|g' \
  -e 's|[^:[:space:]]+/control-description\.txt:|debian/control:|g' \
  -e 's|[^:[:space:]]+/rules-comments\.txt:|debian/rules:|g' \
  "${spellcheck_output}"

exit "${spellcheck_status}"
