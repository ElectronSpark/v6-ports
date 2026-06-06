#!/usr/bin/env bash
set -euo pipefail

wrapper="${1:?wrapper binary required}"
sysroot="${2:?sysroot required}"

wrap_one() {
    local dir="$1"
    local exe="$2"
    local path="${dir}/${exe}"
    local real="${path}.real"
    local payload="${path}.payload"

    [[ -f "${path}" || -f "${real}" || -f "${payload}" ]] || return 0
    if [[ ! -f "${payload}" ]]; then
        if [[ -f "${real}" ]]; then
            mv "${real}" "${payload}"
        elif [[ -f "${path}" ]]; then
            mv "${path}" "${payload}"
        fi
    fi
    cp "${wrapper}" "${path}"
    cp "${wrapper}" "${real}"
    chmod 0755 "${path}"
    chmod 0755 "${real}"
}

for exe in WebKitNetworkProcess WebKitWebProcess WebKitGPUProcess; do
    wrap_one "${sysroot}/libexec/webkit2gtk-4.1" "${exe}"
    wrap_one "${sysroot}/lib/webkit2gtk-4.1" "${exe}"
done
