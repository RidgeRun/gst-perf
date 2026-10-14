#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

command -v debuild >/dev/null 2>&1 || {
  echo "debuild is required to build the Debian package" >&2
  exit 1
}
command -v dpkg-deb >/dev/null 2>&1 || {
  echo "dpkg-deb is required to check the package layout" >&2
  exit 1
}

package_source="$(awk '$1 == "Source:" { print $2; exit }' debian/control)"
test -n "${package_source}" || {
  echo "Unable to determine the Debian source package name" >&2
  exit 1
}

debuild -us -uc -b

package_file="$(find "$(dirname "${repo_root}")" -maxdepth 1 -type f \
  -name "${package_source}_*.deb" -printf '%T@ %p\n' | \
  sort -nr | head -n 1 | cut -d' ' -f2-)"

test -n "${package_file}" || {
  echo "debuild did not produce a ${package_source} Debian package" >&2
  exit 1
}

dpkg-deb --info "${package_file}" >/dev/null
package_contents="$(mktemp)"
trap 'rm -f "${package_contents}"' EXIT
dpkg-deb --contents "${package_file}" >"${package_contents}"

grep -Eq '\./usr/lib/[^/]+/gstreamer-1\.0/[^/]+\.so$' "${package_contents}" || {
  echo "The package does not contain a GStreamer plugin" >&2
  exit 1
}

grep -Eq "\./usr/share/doc/${package_source}/changelog\.Debian\.gz$" \
  "${package_contents}" || {
  echo "The package does not contain its Debian changelog" >&2
  exit 1
}

grep -Eq "\./usr/share/doc/${package_source}/copyright$" "${package_contents}" || {
  echo "The package does not contain its copyright file" >&2
  exit 1
}

echo "Validated Debian package: ${package_file}"
