#!/usr/bin/env bash

set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
command -v dpkg-buildpackage >/dev/null 2>&1 || {
  echo "dpkg-buildpackage is required to check debian/rules" >&2
  exit 1
}

dpkg-buildpackage -us -uc
