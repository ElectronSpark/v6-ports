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

remove_legacy_libc_glob() {
    local pattern="$1"
    local matches=()
    shopt -s nullglob
    matches=( ${pattern} )
    shopt -u nullglob
    for candidate in "${matches[@]}"; do
        if readelf -d "${candidate}" 2>/dev/null |
           grep -q 'Shared library: \[libc\.so\]'; then
            rm -f "${candidate}"
        fi
    done
}

remove_staged_webkit() {
    mkdir -p "${dst}/libexec/webkit2gtk-4.1"
    rm -rf \
        "${dst}/lib/gstreamer-1.0" \
        "${dst}/lib/webkit2gtk-4.1" \
        "${dst}/include/libsoup-3.0" \
        "${dst}/include/webkitgtk-4.1" \
        "${dst}/usr/lib/gstreamer-1.0" \
        "${dst}/libexec/gstreamer-1.0"
    rm -f \
        "${dst}/bin/gst-inspect-1.0" \
        "${dst}/bin/gst-launch-1.0" \
        "${dst}/bin/gst-typefind-1.0" \
        "${dst}/bin/jsc" \
        "${dst}/lib/libgst"*.so* \
        "${dst}/lib/libgstreamer-1.0.so"* \
        "${dst}/lib/gio/modules/libgioopenssl.so" \
        "${dst}/lib/libwebkit2gtk-4.1.so"* \
        "${dst}/lib/libjavascriptcoregtk-4.1.so"* \
        "${dst}/lib/libnghttp2.so"* \
        "${dst}/lib/libogg.so"* \
        "${dst}/lib/libopus.so"* \
        "${dst}/lib/libvorbis.so"* \
        "${dst}/lib/libvorbisenc.so"* \
        "${dst}/lib/libvorbisfile.so"* \
        "${dst}/lib/libatomic.so"* \
        "${dst}/lib/libstdc++.so"* \
        "${dst}/lib/libgcc_s.so"* \
        "${dst}/lib/pkgconfig/webkit2gtk-4.1.pc" \
        "${dst}/lib/pkgconfig/webkit2gtk-web-extension-4.1.pc" \
        "${dst}/lib/pkgconfig/javascriptcoregtk-4.1.pc" \
        "${dst}/lib/pkgconfig/libsoup-3.0.pc" \
        "${dst}/libexec/webkit2gtk-4.1/MiniBrowser" \
        "${dst}/libexec/webkit2gtk-4.1/WebKitNetworkProcess" \
        "${dst}/libexec/webkit2gtk-4.1/WebKitWebProcess" \
        "${dst}/libexec/webkit2gtk-4.1/jsc" \
        "${dst}/libexec/webkit2gtk-4.1/.webkit_install_stamp" \
        "${dst}/libexec/webkit2gtk-4.1/.webkit-stage-manifest" \
        "${dst}/usr/lib/libgst"*.so*

    # Older staged WebKit bundles carried musl-style dependencies into the
    # shared GTK stack. Keep host-built glibc libraries, but scrub any stale
    # DSO whose dynamic section still asks for plain libc.so.
    for pattern in \
        "${dst}/lib/libgtk-3.so"* \
        "${dst}/lib/libgdk-3.so"* \
        "${dst}/lib/libgdk_pixbuf-2.0.so"* \
        "${dst}/lib/libgio-2.0.so"* \
        "${dst}/lib/libgobject-2.0.so"* \
        "${dst}/lib/libglib-2.0.so"* \
        "${dst}/lib/libgmodule-2.0.so"* \
        "${dst}/lib/libpangocairo-1.0.so"* \
        "${dst}/lib/libpango-1.0.so"* \
        "${dst}/lib/libpangoft2-1.0.so"* \
        "${dst}/lib/libatk-1.0.so"* \
        "${dst}/lib/libcairo-gobject.so"* \
        "${dst}/lib/libcairo.so"* \
        "${dst}/lib/libepoxy.so"* \
        "${dst}/lib/libfontconfig.so"* \
        "${dst}/lib/libfreetype.so"* \
        "${dst}/lib/libpixman-1.so"* \
        "${dst}/lib/libpng16.so"* \
        "${dst}/lib/libfribidi.so"* \
        "${dst}/lib/libxkbcommon.so"* \
        "${dst}/lib/libffi.so"*
    do
        remove_legacy_libc_glob "${pattern}"
    done
}

runtime_uses_legacy_libc() {
    while IFS= read -r -d '' elf; do
        if readelf -l "${elf}" 2>/dev/null | grep -q '/ld-musl-'; then
            echo "${elf}: musl interpreter" >&2
            return 0
        fi
        if readelf -d "${elf}" 2>/dev/null | grep -q 'Shared library: \[libc\.so\]'; then
            echo "${elf}: legacy libc.so dependency" >&2
            return 0
        fi
    done < <(find "${ref}/lib" "${ref}/usr/lib" "${ref}/libexec" "${ref}/bin" \
        -type f \( -perm -111 -o -name '*.so' -o -name '*.so.*' \) \
        -print0 2>/dev/null)
    return 1
}

if [[ -z "${ref}" ]]; then
    echo "ports/webkit: warning: no WebKitGTK runtime selected; skipping stage" >&2
    mkdir -p "${dst}/libexec/webkit2gtk-4.1"
    touch "${dst}/libexec/webkit2gtk-4.1/.webkit-stage.stamp"
    exit 0
fi

if [[ ! -x "${ref}/libexec/webkit2gtk-4.1/MiniBrowser" ]]; then
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

if runtime_uses_legacy_libc; then
    echo "ports/webkit: refusing to stage non-host-glibc WebKitGTK runtime from ${ref}" >&2
    remove_staged_webkit
    touch "${dst}/libexec/webkit2gtk-4.1/.webkit-stage.stamp"
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

elf_exports_symbol() {
    local elf="$1"
    local symbol="$2"

    [[ -e "${elf}" ]] || return 1
    command -v nm >/dev/null 2>&1 || return 1
    if nm -D "${elf}" 2>/dev/null | awk '{ print $3 }' |
       grep -x "${symbol}" >/dev/null; then
        return 0
    fi
    return 1
}

elf_has_undefined_symbol() {
    local elf="$1"
    local symbol="$2"

    [[ -e "${elf}" ]] || return 1
    command -v nm >/dev/null 2>&1 || return 1
    if nm -D "${elf}" 2>/dev/null |
       awk '$1 == "U" { print $2 } $2 == "U" { print $3 }' |
       grep -x "${symbol}" >/dev/null; then
        return 0
    fi
    return 1
}

find_host_library() {
    local soname="$1"
    local candidate

    if command -v ldconfig >/dev/null 2>&1; then
        candidate="$(
            ldconfig -p 2>/dev/null |
            awk -v soname="${soname}" '$1 == soname { print $NF; exit }'
        )"
        if [[ -n "${candidate}" && -e "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    fi

    for candidate in \
        "/lib/x86_64-linux-gnu/${soname}" \
        "/usr/lib/x86_64-linux-gnu/${soname}" \
        "/lib/${soname}" \
        "/usr/lib/${soname}"; do
        if [[ -e "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done

    return 1
}

stage_host_library_soname() {
    local soname="$1"
    local reason="$2"
    local host_lib

    host_lib="$(find_host_library "${soname}" || true)"
    if [[ -z "${host_lib}" ]]; then
        echo "ports/webkit: warning: ${reason}, but no host ${soname} was found" >&2
        return 1
    fi

    echo "ports/webkit: staging host ${soname} for ${reason}" >&2
    cp -L "${host_lib}" "${dst}/lib/${soname}"
    chmod 0755 "${dst}/lib/${soname}" 2>/dev/null || true
    return 0
}

stage_host_x11_runtime() {
    local reason="$1"
    local sonames=(
        libX11.so.6
        libXext.so.6
        libXrender.so.1
        libXi.so.6
        libXcursor.so.1
        libXdamage.so.1
        libXfixes.so.3
        libXcomposite.so.1
        libXrandr.so.2
        libXinerama.so.1
        libxcb.so.1
        libxcb-render.so.0
        libxcb-shm.so.0
        libXau.so.6
        libXdmcp.so.6
        libbsd.so.0
        libmd.so.0
    )
    local soname

    for soname in "${sonames[@]}"; do
        stage_host_library_soname "${soname}" "${reason}" || true
    done
}

sysroot_has_library_soname() {
    local soname="$1"

    [[ -e "${dst}/lib/${soname}" || -e "${dst}/usr/lib/${soname}" ]]
}

is_glibc_baseline_soname() {
    case "$1" in
        libc.so.6|libm.so.6|libdl.so.2|libpthread.so.0|librt.so.1|ld-linux-x86-64.so.2)
            return 0
            ;;
    esac
    return 1
}

queue_elf_if_present() {
    local elf="$1"
    local -n queue_ref="$2"
    local -n seen_ref="$3"

    [[ -e "${elf}" ]] || return 0
    [[ -n "${seen_ref[${elf}]:-}" ]] && return 0
    seen_ref["${elf}"]=1
    queue_ref+=("${elf}")
}

stage_host_needed_closure() {
    local reason="WebKit runtime dependency"
    local queue=()
    local elf
    local soname
    local host_lib
    local staged_lib
    local roots=(
        "${dst}/libexec/webkit2gtk-4.1/MiniBrowser"
        "${dst}/libexec/webkit2gtk-4.1/WebKitNetworkProcess"
        "${dst}/libexec/webkit2gtk-4.1/WebKitWebProcess"
        "${dst}/libexec/webkit2gtk-4.1/jsc"
        "${dst}/lib/libwebkit2gtk-4.1.so.0"
        "${dst}/lib/libjavascriptcoregtk-4.1.so.0"
        "${dst}/lib/webkit2gtk-4.1/injected-bundle/libwebkit2gtkinjectedbundle.so"
    )
    declare -A seen_elf=()

    for elf in "${roots[@]}"; do
        queue_elf_if_present "${elf}" queue seen_elf
    done

    while ((${#queue[@]})); do
        elf="${queue[0]}"
        queue=("${queue[@]:1}")

        while IFS= read -r soname; do
            is_glibc_baseline_soname "${soname}" && continue

            if ! sysroot_has_library_soname "${soname}"; then
                stage_host_library_soname "${soname}" "${reason}" || true
            fi

            if [[ -e "${dst}/lib/${soname}" ]]; then
                queue_elf_if_present "${dst}/lib/${soname}" queue seen_elf
            elif [[ -e "${dst}/usr/lib/${soname}" ]]; then
                queue_elf_if_present "${dst}/usr/lib/${soname}" queue seen_elf
            else
                host_lib="$(find_host_library "${soname}" || true)"
                if [[ -n "${host_lib}" ]]; then
                    queue_elf_if_present "${host_lib}" queue seen_elf
                fi
            fi
        done < <(readelf -dW "${elf}" 2>/dev/null |
            sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
    done
}

stage_host_gdk_x11_if_needed() {
    local webkit_lib="${dst}/lib/libwebkit2gtk-4.1.so.0"
    local staged_gdk="${dst}/lib/libgdk-3.so.0"
    local host_gdk

    if ! elf_has_undefined_symbol "${webkit_lib}" "gdk_x11_cursor_get_xcursor"; then
        return 0
    fi
    if elf_exports_symbol "${staged_gdk}" "gdk_x11_cursor_get_xcursor"; then
        return 0
    fi

    host_gdk="$(find_host_library "libgdk-3.so.0" || true)"
    if [[ -z "${host_gdk}" ]] ||
       ! elf_exports_symbol "${host_gdk}" "gdk_x11_cursor_get_xcursor"; then
        echo "ports/webkit: warning: WebKit needs GDK X11 symbols, but no compatible host libgdk-3.so.0 was found" >&2
        return 0
    fi

    echo "ports/webkit: staging host libgdk-3.so.0 for WebKit GDK X11 ABI compatibility" >&2
    cp -L "${host_gdk}" "${dst}/lib/libgdk-3.so.0"
    chmod 0755 "${dst}/lib/libgdk-3.so.0" 2>/dev/null || true
    ln -sf libgdk-3.so.0 "${dst}/lib/libgdk-3.so"
}

stage_host_cairo_xlib_if_needed() {
    local staged_gdk="${dst}/lib/libgdk-3.so.0"
    local staged_cairo="${dst}/lib/libcairo.so.2"
    local host_cairo
    local reason="GDK X11 Cairo ABI compatibility"

    if ! elf_has_undefined_symbol "${staged_gdk}" "cairo_xlib_surface_get_display"; then
        return 0
    fi
    if elf_exports_symbol "${staged_cairo}" "cairo_xlib_surface_get_display"; then
        return 0
    fi

    host_cairo="$(find_host_library "libcairo.so.2" || true)"
    if [[ -z "${host_cairo}" ]] ||
       ! elf_exports_symbol "${host_cairo}" "cairo_xlib_surface_get_display"; then
        echo "ports/webkit: warning: host libgdk-3.so.0 needs Cairo Xlib symbols, but no compatible host libcairo.so.2 was found" >&2
        return 0
    fi

    stage_host_library_soname "libcairo.so.2" "${reason}" || return 0
    ln -sf libcairo.so.2 "${dst}/lib/libcairo.so"
    stage_host_x11_runtime "${reason}"
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
    "${ref}/lib/libgstgl-1.0.so"*
    "${ref}/lib/libwebpdemux.so"*
    "${ref}/lib/libharfbuzz-icu.so"*
    "${ref}/lib/libmanette-0.2.so"*
    "${ref}/lib/libenchant-2.so"*
    "${ref}/lib/libhyphen.so"*
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
stage_host_gdk_x11_if_needed
stage_host_cairo_xlib_if_needed

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
mkdir -p "${dst}/usr/lib/x86_64-linux-gnu/webkit2gtk-4.1"
for exe in MiniBrowser WebKitNetworkProcess WebKitWebProcess jsc; do
    if [[ -x "${dst}/libexec/webkit2gtk-4.1/${exe}" ]]; then
        ln -sfn "/libexec/webkit2gtk-4.1/${exe}" \
                "${dst}/usr/lib/x86_64-linux-gnu/webkit2gtk-4.1/${exe}"
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
if [[ -d "/usr/share/X11/xkb" ]]; then
    mkdir -p "${dst}/usr/share/X11"
    rm -rf "${dst}/usr/share/X11/xkb"
    cp -a "/usr/share/X11/xkb" "${dst}/usr/share/X11/"
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

stage_host_needed_closure

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
        -o -path "${dst}/libexec/webkit2gtk-4.1/*" \
        -o -path "${dst}/usr/lib/x86_64-linux-gnu/webkit2gtk-4.1/*" \) \
        -printf '%P\n' | sort
} > "${dst}/libexec/webkit2gtk-4.1/.webkit-stage-manifest"

touch "${dst}/libexec/webkit2gtk-4.1/.webkit-stage.stamp"
