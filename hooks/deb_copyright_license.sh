#!/usr/bin/env bash

set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
command -v lintian >/dev/null 2>&1 || {
  echo "lintian is required to check the Debian packaging declaration" >&2
  exit 1
}
command -v licensecheck >/dev/null 2>&1 || {
  echo "licensecheck is required to assess source license coverage" >&2
  exit 1
}
command -v git >/dev/null 2>&1 || {
  echo "git is required to check source provenance" >&2
  exit 1
}
command -v dpkg-deb >/dev/null 2>&1 || {
  echo "dpkg-deb is required to inspect the built package" >&2
  exit 1
}

if grep -q 'GNU LIBRARY GENERAL PUBLIC LICENSE' LICENSE &&
   grep -q 'Version 2, June 1991' LICENSE; then
  expected_license='LGPL-2.0-or-later'
else
  echo "Unable to identify the supported license from LICENSE" >&2
  exit 1
fi

grep -q "^License: ${expected_license}$" debian/copyright || {
  echo "debian/copyright does not match the license identified in LICENSE" >&2
  exit 1
}

package_source="$(awk '$1 == "Source:" { print $2; exit }' debian/control)"
package_file="$(find .. -maxdepth 1 -type f \
  -name "${package_source}_*.deb" -printf '%T@ %p\n' | \
  sort -nr | head -n 1 | cut -d' ' -f2-)"

test -n "${package_file}" || {
  echo "Run the Debian package hook before the copyright hook" >&2
  exit 1
}

lintian --fail-on error "${package_file}"

package_contents="$(mktemp)"
license_report="$(mktemp)"
trap 'rm -f "${package_contents}" "${license_report}"' EXIT
dpkg-deb --contents "${package_file}" >"${package_contents}"

while IFS= read -r package_path; do
  case "${package_path}" in
    ./usr/lib/*/gstreamer-1.0/libgstperf.so) ;;
    ./usr/lib/*/gstreamer-1.0/pkgconfig/gstperf.pc) ;;
    ./usr/share/doc/"${package_source}"/changelog.Debian.gz) ;;
    ./usr/share/doc/"${package_source}"/copyright) ;;
    *)
      echo "Unexpected regular file in package: ${package_path}" >&2
      exit 1
      ;;
  esac
done < <(awk '$1 ~ /^-/ { print $6 }' "${package_contents}")

copyright_sources=(plugins/gstperf.c plugins/gstperf.h)
licensecheck --copyright --deb-machine "${copyright_sources[@]}" \
  | tee "${license_report}"

for source_file in "${copyright_sources[@]}"; do
  git ls-files --error-unmatch -- "${source_file}" >/dev/null || {
    echo "Copyright source is not tracked by Git: ${source_file}" >&2
    exit 1
  }

  grep -Eq "(^|[[:space:]])${source_file}([[:space:]]|$)" debian/copyright || {
    echo "Tracked package source is missing from debian/copyright: ${source_file}" >&2
    exit 1
  }

  git log --format='%ae' -- "${source_file}" \
    | grep -Ei '@ridgerun\.com$' >/dev/null || {
    echo "No RidgeRun Git provenance found for ${source_file}" >&2
    exit 1
  }

  grep -Eq "^Files:.*${source_file}|^ ${source_file}$" "${license_report}" || {
    echo "licensecheck did not report ${source_file}" >&2
    exit 1
  }
done

echo "Verified package file set and Git provenance for ${copyright_sources[*]}"
