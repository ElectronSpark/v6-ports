#!/usr/bin/env bash
set -euo pipefail

ref="${1:-}"
dst="${2:?destination sysroot required}"
strict="${3:-auto}"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
bundled_runtime="${script_dir}/sysroot"
host_gst_plugin_dir="/usr/lib/x86_64-linux-gnu/gstreamer-1.0"
host_gst_exec_dir="/usr/lib/x86_64-linux-gnu/gstreamer1.0/gstreamer-1.0"
host_gst_plugins=(
    libgstadaptivedemux2.so
    libgstapp.so
    libgstaudioconvert.so
    libgstaudiofx.so
    libgstaudioparsers.so
    libgstaudioresample.so
    libgstautodetect.so
    libgstcoreelements.so
    libgstdebugutilsbad.so
    libgstisomp4.so
    libgstlibav.so
    libgstmatroska.so
    libgstogg.so
    libgstopus.so
    libgstossaudio.so
    libgstplayback.so
    libgstsubenc.so
    libgstsubparse.so
    libgsttypefindfunctions.so
    libgstvideoconvertscale.so
    libgstvideoparsersbad.so
    libgstvideorate.so
    libgstvolume.so
    libgstvpx.so
)
host_gst_plugin_dirs=()
if [[ -n "${HOST_GST_PLUGIN_DIRS:-}" ]]; then
    IFS=: read -r -a host_gst_plugin_dirs <<< "${HOST_GST_PLUGIN_DIRS}"
fi
if [[ -n "${HOST_GST_PLUGIN_DIR:-}" ]]; then
    host_gst_plugin_dirs+=("${HOST_GST_PLUGIN_DIR}")
fi
host_gst_plugin_dirs+=("${host_gst_plugin_dir}")
host_library_dirs=()
if [[ -n "${HOST_LIBRARY_DIRS:-}" ]]; then
    IFS=: read -r -a host_library_dirs <<< "${HOST_LIBRARY_DIRS}"
fi

required_paths=(
    "libexec/webkit2gtk-4.1/MiniBrowser"
    "libexec/webkit2gtk-4.1/WebKitNetworkProcess"
    "libexec/webkit2gtk-4.1/WebKitWebProcess"
    "libexec/webkit2gtk-4.1/jsc"
    "lib/libwebkit2gtk-4.1.so"
    "lib/libjavascriptcoregtk-4.1.so"
    "lib/webkit2gtk-4.1/injected-bundle/libwebkit2gtkinjectedbundle.so"
)

host_gst_plugins_available() {
    local dir
    for dir in "${host_gst_plugin_dirs[@]}"; do
        if [[ -d "${dir}" ]]; then
            return 0
        fi
    done
    return 1
}

host_gst_plugins_complete() {
    local plugin

    for plugin in "${host_gst_plugins[@]}"; do
        if ! host_gst_plugin_path "${plugin}" >/dev/null; then
            return 1
        fi
    done
    return 0
}

host_gst_plugin_path() {
    local plugin="$1"
    local dir
    for dir in "${host_gst_plugin_dirs[@]}"; do
        if [[ -e "${dir}/${plugin}" ]]; then
            printf '%s\n' "${dir}/${plugin}"
            return 0
        fi
    done
    return 1
}

download_deb_chunk() {
    local deb_dir="$1"
    shift

    (cd "${deb_dir}" && apt-get download "$@" >/dev/null 2>&1) && return 0

    local pkg
    for pkg in "$@"; do
        if ! (cd "${deb_dir}" && apt-get download "${pkg}" >/dev/null 2>&1); then
            echo "ports/webkit: warning: could not download ${pkg} for cached GStreamer runtime" >&2
        fi
    done
}

prepare_host_gst_runtime_cache() {
    local cache
    local root
    local debs
    local stamp
    local pkg
    local deb
    local packages=(
        gstreamer1.0-libav
        gstreamer1.0-plugins-bad
        libgstreamer-plugins-bad1.0-0
    )
    local all_packages=()
    local chunk=()

    [[ "${WEBKIT_DOWNLOAD_HOST_GST_RUNTIME:-1}" != "0" ]] || return 0
    host_gst_plugins_complete && return 0
    command -v apt-cache >/dev/null 2>&1 || return 0
    command -v apt-get >/dev/null 2>&1 || return 0
    command -v dpkg-deb >/dev/null 2>&1 || return 0

    if ! apt-cache show "${packages[0]}" >/dev/null 2>&1; then
        echo "ports/webkit: refreshing apt metadata for cached GStreamer runtime" >&2
        if ! apt-get update >/dev/null 2>&1; then
            echo "ports/webkit: warning: apt metadata refresh failed; using installed host GStreamer runtime only" >&2
            return 0
        fi
    fi

    cache="${WEBKIT_HOST_GST_RUNTIME_CACHE:-${dst}/../webkit-gst-runtime-cache}"
    root="${cache}/root"
    debs="${cache}/debs"
    stamp="${cache}/.extract.stamp"

    mkdir -p "${debs}" "${root}"

    if [[ ! -e "${stamp}" ]]; then
        echo "ports/webkit: preparing cached host GStreamer runtime in ${cache}" >&2
        mapfile -t all_packages < <(
            {
                printf '%s\n' "${packages[@]}"
                apt-cache depends --recurse \
                    --no-recommends --no-suggests --no-conflicts \
                    --no-breaks --no-replaces --no-enhances \
                    "${packages[@]}" 2>/dev/null |
                    awk '/^[[:space:]]*(Pre)?Depends:/ { print $2 }'
            } |
            awk '/^[[:alnum:]][[:alnum:].+:-]*$/ { print }' |
            LC_ALL=C sort -u
        )
        for pkg in "${all_packages[@]}"; do
            chunk+=("${pkg}")
            if ((${#chunk[@]} >= 48)); then
                download_deb_chunk "${debs}" "${chunk[@]}"
                chunk=()
            fi
        done
        if ((${#chunk[@]})); then
            download_deb_chunk "${debs}" "${chunk[@]}"
        fi
        rm -rf "${root}"
        mkdir -p "${root}"
        shopt -s nullglob
        for deb in "${debs}"/*.deb; do
            dpkg-deb -x "${deb}" "${root}"
        done
        shopt -u nullglob
        touch "${stamp}"
    fi

    if [[ -d "${root}/usr/lib/x86_64-linux-gnu/gstreamer-1.0" ]]; then
        host_gst_plugin_dirs=(
            "${root}/usr/lib/x86_64-linux-gnu/gstreamer-1.0"
            "${host_gst_plugin_dirs[@]}"
        )
    fi
    if [[ -d "${root}/usr/lib/x86_64-linux-gnu" ]]; then
        host_library_dirs=(
            "${root}/usr/lib/x86_64-linux-gnu"
            "${host_library_dirs[@]}"
        )
    fi
}

prepare_host_gio_tls_runtime_cache() {
    local cache
    local root
    local debs
    local stamp
    local pkg
    local deb
    local packages=(
        glib-networking
    )
    local all_packages=()
    local chunk=()

    [[ "${WEBKIT_DOWNLOAD_HOST_GIO_TLS_RUNTIME:-1}" != "0" ]] || return 0
    command -v apt-cache >/dev/null 2>&1 || return 0
    command -v apt-get >/dev/null 2>&1 || return 0
    command -v dpkg-deb >/dev/null 2>&1 || return 0

    if ! apt-cache show "${packages[0]}" >/dev/null 2>&1; then
        echo "ports/webkit: refreshing apt metadata for cached GIO TLS runtime" >&2
        if ! apt-get update >/dev/null 2>&1; then
            echo "ports/webkit: warning: apt metadata refresh failed; using installed host GIO TLS runtime only" >&2
            return 0
        fi
    fi

    cache="${WEBKIT_HOST_GIO_TLS_RUNTIME_CACHE:-${dst}/../webkit-gio-tls-runtime-cache}"
    root="${cache}/root"
    debs="${cache}/debs"
    stamp="${cache}/.extract.stamp"

    mkdir -p "${debs}" "${root}"

    if [[ ! -e "${stamp}" ]]; then
        echo "ports/webkit: preparing cached host GIO TLS runtime in ${cache}" >&2
        mapfile -t all_packages < <(
            {
                printf '%s\n' "${packages[@]}"
                apt-cache depends --recurse \
                    --no-recommends --no-suggests --no-conflicts \
                    --no-breaks --no-replaces --no-enhances \
                    "${packages[@]}" 2>/dev/null |
                    awk '/^[[:space:]]*(Pre)?Depends:/ { print $2 }'
            } |
            awk '/^[[:alnum:]][[:alnum:].+:-]*$/ { print }' |
            LC_ALL=C sort -u
        )
        for pkg in "${all_packages[@]}"; do
            chunk+=("${pkg}")
            if ((${#chunk[@]} >= 48)); then
                download_deb_chunk "${debs}" "${chunk[@]}"
                chunk=()
            fi
        done
        if ((${#chunk[@]})); then
            download_deb_chunk "${debs}" "${chunk[@]}"
        fi
        rm -rf "${root}"
        mkdir -p "${root}"
        shopt -s nullglob
        for deb in "${debs}"/*.deb; do
            dpkg-deb -x "${deb}" "${root}"
        done
        shopt -u nullglob
        touch "${stamp}"
    fi

    if [[ -d "${root}/usr/lib/x86_64-linux-gnu" ]]; then
        host_library_dirs=(
            "${root}/usr/lib/x86_64-linux-gnu"
            "${host_library_dirs[@]}"
        )
    fi
}

prepare_host_webkit_runtime_dependency_cache() {
    local cache
    local root
    local debs
    local stamp
    local pkg
    local deb
    local packages=(
        libevdev2
        libsecret-1-0
    )
    local all_packages=()
    local chunk=()

    [[ "${WEBKIT_DOWNLOAD_HOST_RUNTIME_DEPS:-1}" != "0" ]] || return 0
    command -v apt-cache >/dev/null 2>&1 || return 0
    command -v apt-get >/dev/null 2>&1 || return 0
    command -v dpkg-deb >/dev/null 2>&1 || return 0

    if ! apt-cache show "${packages[0]}" >/dev/null 2>&1; then
        echo "ports/webkit: refreshing apt metadata for cached WebKit runtime dependencies" >&2
        if ! apt-get update >/dev/null 2>&1; then
            echo "ports/webkit: warning: apt metadata refresh failed; using installed host WebKit runtime dependencies only" >&2
            return 0
        fi
    fi

    cache="${WEBKIT_HOST_RUNTIME_DEPS_CACHE:-${dst}/../webkit-runtime-deps-cache}"
    root="${cache}/root"
    debs="${cache}/debs"
    stamp="${cache}/.extract.stamp"

    mkdir -p "${debs}" "${root}"

    if [[ ! -e "${stamp}" ]]; then
        echo "ports/webkit: preparing cached host WebKit runtime dependencies in ${cache}" >&2
        mapfile -t all_packages < <(
            {
                printf '%s\n' "${packages[@]}"
                apt-cache depends --recurse \
                    --no-recommends --no-suggests --no-conflicts \
                    --no-breaks --no-replaces --no-enhances \
                    "${packages[@]}" 2>/dev/null |
                    awk '/^[[:space:]]*(Pre)?Depends:/ { print $2 }'
            } |
            awk '/^[[:alnum:]][[:alnum:].+:-]*$/ { print }' |
            LC_ALL=C sort -u
        )
        for pkg in "${all_packages[@]}"; do
            chunk+=("${pkg}")
            if ((${#chunk[@]} >= 48)); then
                download_deb_chunk "${debs}" "${chunk[@]}"
                chunk=()
            fi
        done
        if ((${#chunk[@]})); then
            download_deb_chunk "${debs}" "${chunk[@]}"
        fi
        rm -rf "${root}"
        mkdir -p "${root}"
        shopt -s nullglob
        for deb in "${debs}"/*.deb; do
            dpkg-deb -x "${deb}" "${root}"
        done
        shopt -u nullglob
        touch "${stamp}"
    fi

    if [[ -d "${root}/usr/lib/x86_64-linux-gnu" ]]; then
        host_library_dirs=(
            "${root}/usr/lib/x86_64-linux-gnu"
            "${host_library_dirs[@]}"
        )
    fi
}

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
        "${dst}/lib/libc.so" \
        "${dst}/lib/pkgconfig/webkit2gtk-4.1.pc" \
        "${dst}/lib/pkgconfig/webkit2gtk-web-extension-4.1.pc" \
        "${dst}/lib/pkgconfig/javascriptcoregtk-4.1.pc" \
        "${dst}/lib/pkgconfig/libsoup-3.0.pc" \
        "${dst}/libexec/webkit2gtk-4.1/MiniBrowser" \
        "${dst}/libexec/webkit2gtk-4.1/MiniBrowser.real" \
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
prepare_host_gst_runtime_cache

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
    if nm -D "${elf}" 2>/dev/null |
       awk -v symbol="${symbol}" '$3 == symbol { found = 1 } END { exit !found }'; then
        return 0
    fi
    return 1
}

elf_exports_symbol_prefix() {
    local elf="$1"
    local prefix="$2"

    [[ -e "${elf}" ]] || return 1
    command -v nm >/dev/null 2>&1 || return 1
    if nm -D "${elf}" 2>/dev/null |
       awk -v prefix="${prefix}" 'index($3, prefix) == 1 { found = 1 } END { exit !found }'; then
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
       awk -v symbol="${symbol}" '
           ($1 == "U" && $2 == symbol) || ($2 == "U" && $3 == symbol) { found = 1 }
           END { exit !found }
       '; then
        return 0
    fi
    return 1
}

find_host_library() {
    local soname="$1"
    local candidate
    local nested_candidate

    for candidate in "${host_library_dirs[@]}"; do
        if [[ -e "${candidate}/${soname}" ]]; then
            printf '%s\n' "${candidate}/${soname}"
            return 0
        fi
        for nested_candidate in "${candidate}"/*/"${soname}"; do
            if [[ -e "${nested_candidate}" ]]; then
                printf '%s\n' "${nested_candidate}"
                return 0
            fi
        done
    done

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

    for nested_candidate in \
        /lib/x86_64-linux-gnu/*/"${soname}" \
        /usr/lib/x86_64-linux-gnu/*/"${soname}" \
        /lib/*/"${soname}" \
        /usr/lib/*/"${soname}"; do
        if [[ -e "${nested_candidate}" ]]; then
            printf '%s\n' "${nested_candidate}"
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
        "${dst}/lib/gio/modules/libgiognutls.so"
        "${dst}/lib/gio/modules/libgioopenssl.so"
    )
    declare -A seen_elf=()

    for plugin_dir in "${dst}/lib/gstreamer-1.0" "${dst}/usr/lib/gstreamer-1.0"; do
        if [[ -d "${plugin_dir}" ]]; then
            while IFS= read -r -d '' elf; do
                queue_elf_if_present "${elf}" queue seen_elf
            done < <(find "${plugin_dir}" -maxdepth 1 -type f -name '*.so' -print0)
        fi
    done

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

localize_png_exports() {
    local soname="$1"
    local staged_lib="${dst}/lib/${soname}"
    local target
    local tmp
    local map
    local mode
    local tag

    [[ -e "${staged_lib}" ]] || return 0
    if ! elf_exports_symbol_prefix "${staged_lib}" "png_"; then
        return 0
    fi
    if ! command -v objcopy >/dev/null 2>&1; then
        return 1
    fi

    target="$(readlink -f "${staged_lib}")"
    tmp="${target}.pngiso.$$"
    map="${target}.pngiso.$$.map"
    mode="$(stat -c '%a' "${target}" 2>/dev/null || printf '0755')"
    tag="${soname//[^[:alnum:]]/_}"
    nm -D --defined-only "${target}" 2>/dev/null |
        awk -v tag="${tag}" 'index($3, "png_") == 1 {
            print $3 " __xv6_pngiso_" tag "_" $3
        }' > "${map}"
    if [[ ! -s "${map}" ]]; then
        rm -f "${map}"
        return 0
    fi

    echo "ports/webkit: renaming embedded png_* exports in ${soname}" >&2
    if objcopy --redefine-syms="${map}" "${target}" "${tmp}" &&
       ! elf_exports_symbol_prefix "${tmp}" "png_"; then
        chmod "${mode}" "${tmp}" 2>/dev/null || true
        mv -f "${tmp}" "${target}"
        rm -f "${map}"
        return 0
    fi
    rm -f "${tmp}" "${map}"
    echo "ports/webkit: warning: unable to hide embedded png_* exports in ${soname}; MiniBrowser will preload libpng16" >&2
    return 0
}

restore_ref_library_for_png_isolation() {
    local stem="$1"

    if compgen -G "${ref}/lib/${stem}*" >/dev/null; then
        copy_glob "${ref}/lib/${stem}*"
    fi
}

stage_host_library_if_png_interposes() {
    local soname="$1"
    local link_name="${2:-}"
    local staged_lib="${dst}/lib/${soname}"
    local host_lib
    local reason="WebKit PNG symbol isolation"

    if [[ ! -e "${staged_lib}" ]]; then
        return 0
    fi
    if ! elf_exports_symbol_prefix "${staged_lib}" "png_"; then
        return 0
    fi
    if localize_png_exports "${soname}"; then
        return 0
    fi

    host_lib="$(find_host_library "${soname}" || true)"
    if [[ -z "${host_lib}" ]]; then
        echo "ports/webkit: warning: staged ${soname} exports png_* symbols, but no host ${soname} was found" >&2
        return 0
    fi
    if elf_exports_symbol_prefix "${host_lib}" "png_"; then
        echo "ports/webkit: warning: host ${soname} also exports png_* symbols; keeping staged copy" >&2
        return 0
    fi

    stage_host_library_soname "${soname}" "${reason}" || return 0
    if [[ -n "${link_name}" ]]; then
        ln -sf "${soname}" "${dst}/lib/${link_name}"
    fi
}

stage_host_png_symbol_isolation() {
    restore_ref_library_for_png_isolation "libgdk-3.so"
    restore_ref_library_for_png_isolation "libcairo.so"
    restore_ref_library_for_png_isolation "libharfbuzz.so"

    stage_host_library_if_png_interposes "libgdk-3.so.0" "libgdk-3.so"
    stage_host_cairo_xlib_if_needed
    stage_host_library_if_png_interposes "libcairo.so.2" "libcairo.so"
    stage_host_library_if_png_interposes "libharfbuzz.so.0" "libharfbuzz.so"
}

stage_host_gio_tls_backend() {
    local module_dir="${dst}/lib/gio/modules"
    local host_module=""
    local candidate

    mkdir -p "${module_dir}"

    if [[ -e "${ref}/lib/gio/modules/libgioopenssl.so" ]]; then
        cp -a "${ref}/lib/gio/modules/libgioopenssl.so" "${module_dir}/"
        if [[ -e "${ref}/lib/gio/modules/giomodule.cache" ]]; then
            cp -a "${ref}/lib/gio/modules/giomodule.cache" "${module_dir}/"
        else
            printf 'libgioopenssl.so: gio-tls-backend\n' > "${module_dir}/giomodule.cache"
        fi
        return 0
    fi

    prepare_host_gio_tls_runtime_cache

    for candidate in "${host_library_dirs[@]}"; do
        if [[ -e "${candidate}/gio/modules/libgiognutls.so" ]]; then
            host_module="${candidate}/gio/modules/libgiognutls.so"
            break
        fi
    done

    for candidate in \
        /usr/lib/x86_64-linux-gnu/gio/modules/libgiognutls.so \
        /lib/x86_64-linux-gnu/gio/modules/libgiognutls.so \
        /usr/lib/gio/modules/libgiognutls.so \
        /lib/gio/modules/libgiognutls.so; do
        [[ -z "${host_module}" ]] || break
        if [[ -e "${candidate}" ]]; then
            host_module="${candidate}"
            break
        fi
    done

    if [[ -z "${host_module}" ]]; then
        echo "ports/webkit: warning: no GIO TLS backend module found; HTTPS will be unavailable in WebKit" >&2
        return 0
    fi

    echo "ports/webkit: staging host libgiognutls.so for GIO TLS support" >&2
    cp -L "${host_module}" "${module_dir}/libgiognutls.so"
    chmod 0755 "${module_dir}/libgiognutls.so" 2>/dev/null || true
    printf 'libgiognutls.so: gio-tls-backend\n' > "${module_dir}/giomodule.cache"
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
stage_host_png_symbol_isolation
stage_host_gio_tls_backend
prepare_host_webkit_runtime_dependency_cache

usr_lib_patterns=(
    "${ref}/usr/lib/libgst"*.so*
)

for pattern in "${usr_lib_patterns[@]}"; do
    copy_usr_glob "${pattern}"
done

gst_plugin_ref="${ref}"
gst_plugin_source="ref"
if [[ ! -d "${gst_plugin_ref}/lib/gstreamer-1.0" &&
      ! -d "${gst_plugin_ref}/usr/lib/gstreamer-1.0" &&
      host_gst_plugins_available ]]; then
    echo "ports/webkit: warning: ${ref} has no GStreamer plugins; using host glibc plugin runtime" >&2
    gst_plugin_source="host"
elif [[ ! -d "${gst_plugin_ref}/lib/gstreamer-1.0" &&
        ! -d "${gst_plugin_ref}/usr/lib/gstreamer-1.0" &&
        ( -d "${bundled_runtime}/lib/gstreamer-1.0" ||
        -d "${bundled_runtime}/usr/lib/gstreamer-1.0" ) ]]; then
    echo "ports/webkit: warning: ${ref} has no GStreamer plugins; using bundled plugin runtime" >&2
    gst_plugin_ref="${bundled_runtime}"
    gst_plugin_source="sysroot"
fi
if [[ "${gst_plugin_source}" == "host" ]]; then
    for pattern in \
        "/usr/lib/x86_64-linux-gnu/libgstreamer-1.0.so"* \
        "/usr/lib/x86_64-linux-gnu/libgst"*.so*
    do
        copy_glob "${pattern}"
    done
elif [[ "${gst_plugin_ref}" != "${ref}" ]]; then
    for pattern in \
        "${gst_plugin_ref}/lib/libgstreamer-1.0.so"* \
        "${gst_plugin_ref}/lib/libgst"*.so*
    do
        copy_glob "${pattern}"
    done
    for pattern in "${gst_plugin_ref}/usr/lib/libgst"*.so*; do
        copy_usr_glob "${pattern}"
    done
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
if [[ "${gst_plugin_source}" == "host" ]]; then
    mkdir -p "${dst}/lib/gstreamer-1.0"
    plugin_path=""
    for plugin in "${host_gst_plugins[@]}"; do
        if plugin_path="$(host_gst_plugin_path "${plugin}")"; then
            cp -a "${plugin_path}" "${dst}/lib/gstreamer-1.0/"
        else
            echo "ports/webkit: warning: host GStreamer plugin ${plugin} not found" >&2
        fi
    done
elif [[ -d "${gst_plugin_ref}/lib/gstreamer-1.0" ]]; then
    mkdir -p "${dst}/lib"
    mkdir -p "${dst}/lib/gstreamer-1.0"
    cp -a "${gst_plugin_ref}/lib/gstreamer-1.0"/. "${dst}/lib/gstreamer-1.0/"
fi
if [[ -d "${gst_plugin_ref}/usr/lib/gstreamer-1.0" ]]; then
    mkdir -p "${dst}/usr/lib"
    mkdir -p "${dst}/usr/lib/gstreamer-1.0"
    cp -a "${gst_plugin_ref}/usr/lib/gstreamer-1.0"/. "${dst}/usr/lib/gstreamer-1.0/"
fi
if [[ -d "/usr/share/X11/xkb" ]]; then
    mkdir -p "${dst}/usr/share/X11"
    rm -rf "${dst}/usr/share/X11/xkb"
    cp -a "/usr/share/X11/xkb" "${dst}/usr/share/X11/"
fi
if [[ "${gst_plugin_source}" == "host" && -x "${host_gst_exec_dir}/gst-plugin-scanner" ]]; then
    mkdir -p "${dst}/libexec/gstreamer-1.0"
    cp -a "${host_gst_exec_dir}/gst-plugin-scanner" \
          "${dst}/libexec/gstreamer-1.0/"
elif [[ -x "${gst_plugin_ref}/libexec/gstreamer-1.0/gst-plugin-scanner" ]]; then
    mkdir -p "${dst}/libexec/gstreamer-1.0"
    cp -a "${gst_plugin_ref}/libexec/gstreamer-1.0/gst-plugin-scanner" \
          "${dst}/libexec/gstreamer-1.0/"
fi
for gst_tool in gst-inspect-1.0 gst-launch-1.0 gst-typefind-1.0; do
    if [[ "${gst_plugin_source}" == "host" && -x "/usr/bin/${gst_tool}" ]]; then
        cp -a "/usr/bin/${gst_tool}" "${dst}/bin/"
    elif [[ -x "${gst_plugin_ref}/bin/${gst_tool}" ]]; then
        cp -a "${gst_plugin_ref}/bin/${gst_tool}" "${dst}/bin/"
    fi
done

stage_host_needed_closure
stage_host_png_symbol_isolation
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
