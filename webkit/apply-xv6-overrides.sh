#!/usr/bin/env bash
# Historical compatibility entry point.
#
# The xv6 WebKitGTK port no longer carries repo-local WebKitGTK source patches.
# Keep this script as a no-op validator so old build notes fail clearly on a bad
# source path without mutating upstream source trees.
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <webkitgtk-2.42.5-source-dir>" >&2
    exit 2
fi

src="$1"
if [[ ! -d "${src}" || ! -f "${src}/CMakeLists.txt" ]]; then
    echo "ports/webkit: ${src} is not a WebKitGTK source tree" >&2
    exit 1
fi

echo "ports/webkit: no xv6 WebKitGTK source patches are applied"
