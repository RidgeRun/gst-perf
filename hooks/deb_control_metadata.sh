#!/usr/bin/env bash

set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
command -v dpkg-checkbuilddeps >/dev/null 2>&1 || {
  echo "dpkg-checkbuilddeps is required to check debian/control" >&2
  exit 1
}
command -v dpkg-gencontrol >/dev/null 2>&1 || {
  echo "dpkg-gencontrol is required to check debian/control" >&2
  exit 1
}

dpkg-checkbuilddeps

package_name="$(awk '$1 == "Package:" { print $2; exit }' debian/control)"
test -n "${package_name}" || {
  echo "Unable to determine the binary package name from debian/control" >&2
  exit 1
}

control_dir="$(mktemp -d)"
mkdir -p "${control_dir}/DEBIAN"
trap 'rm -rf "${control_dir}"' EXIT

dpkg-gencontrol -p"${package_name}" -P"${control_dir}"
test -s "${control_dir}/DEBIAN/control"
