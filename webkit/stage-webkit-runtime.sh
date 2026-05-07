#!/usr/bin/env bash
set -euo pipefail

ref="${1:-}"
dst="${2:?destination sysroot required}"
strict="${3:-auto}"

required_paths=(
    "libexec/webkit2gtk-4.1/MiniBrowser"
    "libexec/webkit2gtk-4.1/WebKitNetworkProcess"
    "libexec/webkit2gtk-4.1/WebKitWebProcess"
    "libexec/webkit2gtk-4.1/jsc"
    "lib/libwebkit2gtk-4.1.so"
    "lib/libjavascriptcoregtk-4.1.so"
    "lib/webkit2gtk-4.1/injected-bundle/libwebkit2gtkinjectedbundle.so"
)

remove_staged_webkit() {
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
}

if [[ -z "${ref}" || ! -x "${ref}/libexec/webkit2gtk-4.1/MiniBrowser" ]]; then
    if [[ "${strict}" == "1" || "${strict}" == "ON" || "${strict}" == "TRUE" ]]; then
        echo "ports/webkit: ${ref} does not contain a runnable WebKitGTK runtime" >&2
        exit 1
    fi
    echo "ports/webkit: warning: ${ref} does not contain WebKitGTK; skipping stage" >&2
    remove_staged_webkit
    touch "${dst}/libexec/webkit2gtk-4.1/.webkit-stage.stamp"
    exit 0
fi

missing=()
for rel in "${required_paths[@]}"; do
    if [[ ! -e "${ref}/${rel}" ]]; then
        missing+=("${rel}")
    fi
done
if ((${#missing[@]})); then
    printf 'ports/webkit: incomplete WebKitGTK runtime at %s\n' "${ref}" >&2
    printf '  missing: %s\n' "${missing[@]}" >&2
    exit 1
fi

mkdir -p \
    "${dst}/bin" \
    "${dst}/include" \
    "${dst}/lib" \
    "${dst}/lib/gio/modules" \
    "${dst}/lib/gstreamer-1.0" \
    "${dst}/lib/pkgconfig" \
    "${dst}/lib/webkit2gtk-4.1" \
    "${dst}/libexec" \
    "${dst}/libexec/gstreamer-1.0" \
    "${dst}/usr/lib" \
    "${dst}/share"

remove_staged_webkit

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

copy_usr_glob() {
    local pattern="$1"
    local matches=()
    shopt -s nullglob
    matches=( ${pattern} )
    shopt -u nullglob
    if ((${#matches[@]})); then
        mkdir -p "${dst}/usr/lib"
        cp -a "${matches[@]}" "${dst}/usr/lib/"
    fi
}

# Keep the existing GTK/GLib foundation coherent.  The WebKit/GStreamer
# reference runtime is allowed to refresh WebKit and media pieces, but mixing a
# newer GLib/GIO/GObject/GModule set with the older staged GTK/GDK pair hangs
# GDK Wayland display initialization before gtk_init() returns.
preserve_existing_glob() {
    local pattern="$1"
    local backup_dir="$2"
    local matches=()
    shopt -s nullglob
    matches=( ${pattern} )
    shopt -u nullglob
    if ((${#matches[@]})); then
        cp -a "${matches[@]}" "${backup_dir}/"
    fi
}

preserve_existing_runtime_stack() {
    local backup_dir="$1"
    mkdir -p "${backup_dir}"
    local patterns=(
        "${dst}/lib/libgtk-3.so"*
        "${dst}/lib/libgdk-3.so"*
        "${dst}/lib/libgdk_pixbuf-2.0.so"*
        "${dst}/lib/libgio-2.0.so"*
        "${dst}/lib/libgobject-2.0.so"*
        "${dst}/lib/libglib-2.0.so"*
        "${dst}/lib/libgmodule-2.0.so"*
        "${dst}/lib/libpangocairo-1.0.so"*
        "${dst}/lib/libpango-1.0.so"*
        "${dst}/lib/libpangoft2-1.0.so"*
        "${dst}/lib/libatk-1.0.so"*
        "${dst}/lib/libcairo-gobject.so"*
        "${dst}/lib/libcairo.so"*
    )
    for pattern in "${patterns[@]}"; do
        preserve_existing_glob "${pattern}" "${backup_dir}"
    done
}

restore_existing_runtime_stack() {
    local backup_dir="$1"
    if [[ -d "${backup_dir}" ]] && compgen -G "${backup_dir}/*" > /dev/null; then
        cp -a "${backup_dir}"/. "${dst}/lib/"
    fi
}

runtime_stack_backup="$(mktemp -d)"
trap 'rm -rf "${runtime_stack_backup}"' EXIT
preserve_existing_runtime_stack "${runtime_stack_backup}"

lib_patterns=(
    "${ref}/lib/libgstreamer-1.0.so"*
    "${ref}/lib/libgst"*.so*
    "${ref}/lib/libogg.so"*
    "${ref}/lib/libopus.so"*
    "${ref}/lib/libvorbis.so"*
    "${ref}/lib/libvorbisenc.so"*
    "${ref}/lib/libvorbisfile.so"*
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
restore_existing_runtime_stack "${runtime_stack_backup}"

usr_lib_patterns=(
    "${ref}/usr/lib/libgst"*.so*
)

for pattern in "${usr_lib_patterns[@]}"; do
    copy_usr_glob "${pattern}"
done

if [[ -e "${ref}/lib/gio/modules/libgioopenssl.so" ]]; then
    cp -a "${ref}/lib/gio/modules/libgioopenssl.so" "${dst}/lib/gio/modules/"
fi
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
for pc in \
    webkit2gtk-4.1.pc \
    webkit2gtk-web-extension-4.1.pc \
    javascriptcoregtk-4.1.pc \
    libsoup-3.0.pc
do
    if [[ -e "${ref}/lib/pkgconfig/${pc}" ]]; then
        cp -a "${ref}/lib/pkgconfig/${pc}" "${dst}/lib/pkgconfig/"
    fi
done

if [[ -x "${ref}/bin/jsc" ]]; then
    cp -a "${ref}/bin/jsc" "${dst}/bin/"
fi
if [[ -d "${ref}/lib/gstreamer-1.0" ]]; then
    mkdir -p "${dst}/lib"
    mkdir -p "${dst}/lib/gstreamer-1.0"
    cp -a "${ref}/lib/gstreamer-1.0"/. "${dst}/lib/gstreamer-1.0/"
fi
if [[ -d "${ref}/usr/lib/gstreamer-1.0" ]]; then
    mkdir -p "${dst}/usr/lib"
    mkdir -p "${dst}/usr/lib/gstreamer-1.0"
    cp -a "${ref}/usr/lib/gstreamer-1.0"/. "${dst}/usr/lib/gstreamer-1.0/"
fi
if [[ -x "${ref}/libexec/gstreamer-1.0/gst-plugin-scanner" ]]; then
    mkdir -p "${dst}/libexec/gstreamer-1.0"
    cp -a "${ref}/libexec/gstreamer-1.0/gst-plugin-scanner" \
          "${dst}/libexec/gstreamer-1.0/"
fi
for gst_tool in gst-inspect-1.0 gst-launch-1.0 gst-typefind-1.0; do
    if [[ -x "${ref}/bin/${gst_tool}" ]]; then
        cp -a "${ref}/bin/${gst_tool}" "${dst}/bin/"
    fi
done

manifest_roots=("${dst}/lib" "${dst}/libexec/webkit2gtk-4.1")
[[ -d "${dst}/usr/lib" ]] && manifest_roots+=("${dst}/usr/lib")
[[ -d "${dst}/libexec/gstreamer-1.0" ]] && manifest_roots+=("${dst}/libexec/gstreamer-1.0")
[[ -d "${dst}/lib/gstreamer-1.0" ]] && manifest_roots+=("${dst}/lib/gstreamer-1.0")
[[ -d "${dst}/usr/lib/gstreamer-1.0" ]] && manifest_roots+=("${dst}/usr/lib/gstreamer-1.0")

{
    echo "# Generated by ports/webkit/stage-webkit-runtime.sh"
    echo "source=${ref}"
    find "${manifest_roots[@]}" \
        \( -name 'libwebkit2gtk-4.1.so*' \
        -o -name 'libjavascriptcoregtk-4.1.so*' \
        -o -name 'libgstreamer-1.0.so*' \
        -o -name 'libgst*.so' \
        -o -name 'libgst*.so.*' \
        -o -name 'gst-plugin-scanner' \
        -o -path "${dst}/lib/webkit2gtk-4.1/*" \
        -o -path "${dst}/libexec/webkit2gtk-4.1/*" \) \
        -printf '%P\n' | sort
} > "${dst}/libexec/webkit2gtk-4.1/.webkit-stage-manifest"

touch "${dst}/libexec/webkit2gtk-4.1/.webkit-stage.stamp"
