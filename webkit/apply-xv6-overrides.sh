#!/usr/bin/env bash
# Apply the xv6 WebKitGTK source porting files captured in this repo.
#
# Usage:
#   ports/webkit/apply-xv6-overrides.sh /path/to/webkitgtk-2.42.5
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <webkitgtk-2.42.5-source-dir>" >&2
    exit 2
fi

src="$1"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
overrides="${script_dir}/overrides/webkitgtk-2.42.5"

if [[ ! -d "${src}" || ! -f "${src}/CMakeLists.txt" ]]; then
    echo "ports/webkit: ${src} is not a WebKitGTK source tree" >&2
    exit 1
fi
if [[ ! -d "${overrides}" ]]; then
    echo "ports/webkit: no xv6 WebKitGTK source overrides to apply"
    exit 0
fi

mapfile -t override_files < <(cd "${overrides}" && find . -type f | sed 's#^\./##' | sort)
if [[ ${#override_files[@]} -eq 0 ]]; then
    echo "ports/webkit: no xv6 WebKitGTK source overrides to apply"
    exit 0
fi

for rel in "${override_files[@]}"; do
    mkdir -p "${src}/$(dirname "${rel}")"
    cp -p "${overrides}/${rel}" "${src}/${rel}"
done

echo "ports/webkit: applied xv6 WebKitGTK overrides to ${src}"
