#!/usr/bin/env bash

set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
command -v dpkg-source >/dev/null 2>&1 || {
  echo "dpkg-source is required to build the Debian source package" >&2
  exit 1
}

dpkg-source -b .
