#!/usr/bin/env bash
set -euo pipefail

ref="${1:-}"
dst="${2:?destination sysroot required}"

if [[ -z "${ref}" || ! -x "${ref}/libexec/webkit2gtk-4.1/MiniBrowser" ]]; then
    echo "ports/webkit: warning: ${ref} does not contain WebKitGTK; skipping stage" >&2
    mkdir -p "${dst}/libexec/webkit2gtk-4.1"
    rm -rf \
        "${dst}/lib/webkit2gtk-4.1" \
        "${dst}/include/webkitgtk-4.1"
    rm -f \
        "${dst}/bin/jsc" \
        "${dst}/lib/libwebkit2gtk-4.1.so"* \
        "${dst}/lib/libjavascriptcoregtk-4.1.so"* \
        "${dst}/lib/pkgconfig/webkit2gtk-4.1.pc" \
        "${dst}/lib/pkgconfig/webkit2gtk-web-extension-4.1.pc" \
        "${dst}/lib/pkgconfig/javascriptcoregtk-4.1.pc" \
        "${dst}/libexec/webkit2gtk-4.1/MiniBrowser" \
        "${dst}/libexec/webkit2gtk-4.1/WebKitNetworkProcess" \
        "${dst}/libexec/webkit2gtk-4.1/WebKitWebProcess" \
        "${dst}/libexec/webkit2gtk-4.1/jsc" \
        "${dst}/libexec/webkit2gtk-4.1/.webkit_install_stamp"
    touch "${dst}/libexec/webkit2gtk-4.1/.webkit-stage.stamp"
    exit 0
fi

mkdir -p \
    "${dst}/bin" \
    "${dst}/include" \
    "${dst}/lib" \
    "${dst}/lib/gio/modules" \
    "${dst}/lib/pkgconfig" \
    "${dst}/lib/webkit2gtk-4.1" \
    "${dst}/libexec" \
    "${dst}/share"

copy_glob() {
    local pattern="$1"
    local matches=()
    shopt -s nullglob
    matches=( ${pattern} )
    shopt -u nullglob
    if ((${#matches[@]})); then
        cp -a "${matches[@]}" "${dst}/lib/"
    fi
}

lib_patterns=(
    "${ref}/lib/libwebkit2gtk-4.1.so"*
    "${ref}/lib/libjavascriptcoregtk-4.1.so"*
    "${ref}/lib/libnghttp2.so"*
    "${ref}/lib/libgtk-3.so"*
    "${ref}/lib/libgdk-3.so"*
    "${ref}/lib/libgdk_pixbuf-2.0.so"*
    "${ref}/lib/libgio-2.0.so"*
    "${ref}/lib/libgobject-2.0.so"*
    "${ref}/lib/libglib-2.0.so"*
    "${ref}/lib/libgmodule-2.0.so"*
    "${ref}/lib/libpangocairo-1.0.so"*
    "${ref}/lib/libpango-1.0.so"*
    "${ref}/lib/libpangoft2-1.0.so"*
    "${ref}/lib/libatk-1.0.so"*
    "${ref}/lib/libcairo-gobject.so"*
    "${ref}/lib/libcairo.so"*
    "${ref}/lib/libepoxy.so"*
    "${ref}/lib/libfontconfig.so"*
    "${ref}/lib/libfreetype.so"*
    "${ref}/lib/libpixman-1.so"*
    "${ref}/lib/libpng16.so"*
    "${ref}/lib/libfribidi.so"*
    "${ref}/lib/libxkbcommon.so"*
    "${ref}/lib/libffi.so"*
    "${ref}/lib/libatomic.so"*
    "${ref}/lib/libstdc++.so"*
    "${ref}/lib/libgcc_s.so"*
)

for pattern in "${lib_patterns[@]}"; do
    copy_glob "${pattern}"
done

cp -a "${ref}/lib/gio/modules/libgioopenssl.so" "${dst}/lib/gio/modules/"
if [[ -e "${ref}/lib/gio/modules/giomodule.cache" ]]; then
    cp -a "${ref}/lib/gio/modules/giomodule.cache" "${dst}/lib/gio/modules/"
fi

mkdir -p "${dst}/libexec/webkit2gtk-4.1"
rm -f "${dst}/libexec/webkit2gtk-4.1/MiniBrowser.bak"
for exe in MiniBrowser WebKitNetworkProcess WebKitWebProcess jsc; do
    if [[ -x "${ref}/libexec/webkit2gtk-4.1/${exe}" ]]; then
        cp -a "${ref}/libexec/webkit2gtk-4.1/${exe}" \
              "${dst}/libexec/webkit2gtk-4.1/"
    fi
done
if [[ -e "${ref}/libexec/webkit2gtk-4.1/.webkit_install_stamp" ]]; then
    cp -a "${ref}/libexec/webkit2gtk-4.1/.webkit_install_stamp" \
          "${dst}/libexec/webkit2gtk-4.1/"
fi
cp -a "${ref}/lib/webkit2gtk-4.1" "${dst}/lib/"

if [[ -d "${ref}/include/webkitgtk-4.1" ]]; then
    cp -a "${ref}/include/webkitgtk-4.1" "${dst}/include/"
fi
if [[ -d "${ref}/include/libsoup-3.0" ]]; then
    cp -a "${ref}/include/libsoup-3.0" "${dst}/include/"
fi
cp -a "${ref}/lib/pkgconfig/webkit2gtk-4.1.pc" \
      "${ref}/lib/pkgconfig/webkit2gtk-web-extension-4.1.pc" \
      "${ref}/lib/pkgconfig/javascriptcoregtk-4.1.pc" \
      "${ref}/lib/pkgconfig/libsoup-3.0.pc" \
      "${dst}/lib/pkgconfig/" 2>/dev/null || true

if [[ -x "${ref}/bin/jsc" ]]; then
    cp -a "${ref}/bin/jsc" "${dst}/bin/"
fi

touch "${dst}/libexec/webkit2gtk-4.1/.webkit-stage.stamp"
