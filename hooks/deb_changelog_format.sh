#!/usr/bin/env bash

set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
command -v dpkg-parsechangelog >/dev/null 2>&1 || {
  echo "dpkg-parsechangelog is required to check debian/changelog" >&2
  exit 1
}

diagnostics="$(mktemp)"
trap 'rm -f "${diagnostics}"' EXIT

check_dpkg_command() {
  local command_status=0

  "$@" >/dev/null 2>"${diagnostics}" || command_status=$?

  if [ "${command_status}" -ne 0 ] || grep -qi 'warning' "${diagnostics}"; then
    cat "${diagnostics}" >&2
    return 1
  fi

  : >"${diagnostics}"
}

check_dpkg_command dpkg-parsechangelog --all

# dpkg-genchanges validates the ordering of the current changelog entry
# against the previous package version. Treat its warnings as errors.
check_dpkg_command dpkg-genchanges -S -O/dev/null

changelog_status=0
blank_entry="$(awk '/^[[:space:]]*\*[[:space:]]*$/ { print NR ":" $0 }' \
  debian/changelog)"
if [ -n "${blank_entry}" ]; then
  printf 'Empty changelog entry:\n%s\n' "${blank_entry}" >&2
  changelog_status=1
fi

previous_timestamp=''
while IFS='|' read -r line_number entry_date; do
  if ! current_timestamp="$(date -d "${entry_date}" +%s)"; then
    echo "Unable to parse changelog date on line ${line_number}: ${entry_date}" >&2
    changelog_status=1
    continue
  fi

  if [ -n "${previous_timestamp}" ] &&
     [ "${current_timestamp}" -gt "${previous_timestamp}" ]; then
    echo "Changelog dates are not in descending order at line ${line_number}: ${entry_date}" >&2
    changelog_status=1
  fi

  previous_timestamp="${current_timestamp}"
done < <(awk '/^ -- / { line = $0; sub(/^ -- .*  /, "", line); print NR "|" line }' \
  debian/changelog)

exit "${changelog_status}"
