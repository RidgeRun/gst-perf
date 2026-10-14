#!/usr/bin/env bash

set -u

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}" || exit

checks=(
  "Debian Control|hooks/deb_control_metadata.sh"
  "Debian Source|hooks/deb_source_package.sh"
  "Debian Changelog|hooks/deb_changelog_format.sh"
  "Debian Rules|hooks/deb_rules_build.sh"
  "Debian Package|hooks/deb_package_layout.sh"
  "Debian Copyright|hooks/deb_copyright_license.sh"
  "Debian Spelling|hooks/deb_spelling.sh"
)

overall_status=0
for check in "${checks[@]}"; do
  name="${check%%|*}"
  script="${check#*|}"
  output_file="$(mktemp)"

  if "${script}" >"${output_file}" 2>&1; then
    printf '%s' "${name}"
    printf '%*s' "$((60 - ${#name}))" '' | tr ' ' '.'
    printf 'Passed\n'
  else
    printf '%s' "${name}"
    printf '%*s' "$((60 - ${#name}))" '' | tr ' ' '.'
    printf 'Failed\n'
    sed -n '1,240p' "${output_file}"
    overall_status=1
  fi

  rm -f "${output_file}"
done

exit "${overall_status}"
