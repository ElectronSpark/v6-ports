# Port migration roadmap

See [README.md](README.md) for the port system's general usage. This file
tracks which ports build from source vs. stage from a reference sysroot,
and documents the per-port recipes still to be written.

## Status

| Port      | Status                          | Recipe location                                |
| --------- | ------------------------------- | ---------------------------------------------- |
| zlib      | from-source (cmake)             | `ports/zlib/CMakeLists.txt`                    |
| bzip2     | from-source (cmake)             | `ports/bzip2/CMakeLists.txt`                   |
| xz        | from-source (cmake)             | `ports/xz/CMakeLists.txt`                      |
| libffi    | from-source (autoconf)          | `ports/libffi/CMakeLists.txt`                  |
| sqlite    | from-source (autosetup)         | `ports/sqlite/CMakeLists.txt`                  |
| ncurses   | from-source (autoconf)          | `ports/ncurses/CMakeLists.txt`                 |
| readline  | from-source (autoconf, deps: ncurses) | `ports/readline/CMakeLists.txt`         |
| openssl   | from-source (perl Configure)    | `ports/openssl/CMakeLists.txt`                 |
| libpng    | from-source (cmake, deps: zlib) | `ports/libpng/CMakeLists.txt`                  |
| libjpeg-turbo | from-source (cmake)         | `ports/libjpeg-turbo/CMakeLists.txt`           |
| libexpat  | from-source (cmake)             | `ports/libexpat/CMakeLists.txt`                |
| libxml2   | from-source (cmake, deps: zlib/xz) | `ports/libxml2/CMakeLists.txt`              |
| freetype  | from-source (cmake, deps: zlib/bzip2/libpng) | `ports/freetype/CMakeLists.txt`  |
| curl      | from-source (cmake, deps: zlib/openssl) | `ports/curl/CMakeLists.txt`             |
| pcre2     | from-source (cmake)             | `ports/pcre2/CMakeLists.txt`                   |
| pixman    | from-source (meson)             | `ports/pixman/CMakeLists.txt`                  |
| fribidi   | from-source (meson)             | `ports/fribidi/CMakeLists.txt`                 |
| harfbuzz  | from-source (meson, deps: freetype) | `ports/harfbuzz/CMakeLists.txt`            |
| fontconfig| from-source (meson, deps: freetype/libexpat) | `ports/fontconfig/CMakeLists.txt`  |
| cairo     | from-source (meson, deps: pixman/freetype/fontconfig/libpng/zlib) | `ports/cairo/CMakeLists.txt` |
| glib      | from-source (meson, deps: pcre2/libffi/zlib) | `ports/glib/CMakeLists.txt`         |
| glib-host | native (host tools)             | `ports/glib-host/CMakeLists.txt`               |
| atk       | from-source (meson, deps: glib) | `ports/atk/CMakeLists.txt`                     |
| gdk-pixbuf| from-source (meson, deps: glib/libpng/libjpeg) | `ports/gdk-pixbuf/CMakeLists.txt` |
| pango     | from-source (meson, deps: glib/harfbuzz/fontconfig/cairo/fribidi/freetype) | `ports/pango/CMakeLists.txt` |
| khronos-headers | header-only (Khronos EGL/GL registry) | `ports/khronos-headers/CMakeLists.txt` |
| linux-uapi-headers | header-only (host /usr/include/linux) | `ports/linux-uapi-headers/CMakeLists.txt` |
| libepoxy  | from-source (meson, deps: khronos-headers) | `ports/libepoxy/CMakeLists.txt`     |
| wayland-host | native (host wayland-scanner)  | `ports/wayland-host/CMakeLists.txt`         |
| wayland-libs | from-source (meson, deps: libffi/libexpat/wayland-host) | `ports/wayland-libs/CMakeLists.txt` |
| wayland-protocols | data-only (meson, deps: wayland-libs/wayland-host) | `ports/wayland-protocols/CMakeLists.txt` |
| libxkbcommon | from-source (meson, deps: wayland-libs) | `ports/libxkbcommon/CMakeLists.txt`     |
| gtk3      | from-source (meson, deps: glib/gdk-pixbuf/pango/atk/cairo/wayland-libs/wayland-protocols/libxkbcommon/libepoxy/linux-uapi-headers) | `ports/gtk3/CMakeLists.txt` |
| netsurf-buildsystem | data-only (Makefile fragments) | `ports/netsurf-buildsystem/CMakeLists.txt` |
| netsurf-nsgenbind   | native (host JS-binding generator) | `ports/netsurf-nsgenbind/CMakeLists.txt` |
| netsurf-libwapcaplet | from-source (netsurf-make) | `ports/netsurf-libwapcaplet/CMakeLists.txt` |
| netsurf-libparserutils | from-source (netsurf-make) | `ports/netsurf-libparserutils/CMakeLists.txt` |
| netsurf-libhubbub   | from-source (netsurf-make, deps: libparserutils) | `ports/netsurf-libhubbub/CMakeLists.txt` |
| netsurf-libcss      | from-source (netsurf-make, deps: libwapcaplet/libparserutils) | `ports/netsurf-libcss/CMakeLists.txt` |
| netsurf-libdom      | from-source (netsurf-make, deps: nsgenbind/libexpat/libwapcaplet/libparserutils/libhubbub/libcss) | `ports/netsurf-libdom/CMakeLists.txt` |
| netsurf-libnsgif    | from-source (netsurf-make) | `ports/netsurf-libnsgif/CMakeLists.txt` |
| netsurf-libnsbmp    | from-source (netsurf-make) | `ports/netsurf-libnsbmp/CMakeLists.txt` |
| netsurf-libnsutils  | from-source (netsurf-make) | `ports/netsurf-libnsutils/CMakeLists.txt` |
| netsurf-libnslog    | from-source (netsurf-make) | `ports/netsurf-libnslog/CMakeLists.txt` |
| netsurf-libnspsl    | from-source (netsurf-make) | `ports/netsurf-libnspsl/CMakeLists.txt` |
| netsurf-libsvgtiny  | from-source (netsurf-make, deps: libexpat/libwapcaplet) | `ports/netsurf-libsvgtiny/CMakeLists.txt` |
| vim       | from-source (autoconf, deps: ncurses) | `ports/vim/CMakeLists.txt`               |
| cpython   | from-source (autoconf, deps: all above) | `ports/cpython/CMakeLists.txt`         |
| webkit    | repo-local runtime stage, no active source overrides | `ports/webkit/CMakeLists.txt` |

## NetSurf-GTK3 roadmap (in progress)

Goal: replace the staged `bin/netsurf` with a real from-source GTK3 build.
Dependency tree (~25 new ports). Status legend: ✓ = built, … = pending.

```
Tier 1 (foundation, autoconf/cmake — DONE):
  ✓ zlib  ✓ bzip2  ✓ xz  ✓ libffi  ✓ openssl
  ✓ libpng  ✓ libjpeg-turbo  ✓ libexpat  ✓ libxml2  ✓ freetype  ✓ curl

Tier 2 (font/graphics, mostly meson):
  ✓ pcre2  ✓ pixman  ✓ fribidi  ✓ harfbuzz  ✓ fontconfig  ✓ cairo

Tier 3 (glib stack, meson):
  ✓ glib  ✓ glib-host  ✓ atk  ✓ gdk-pixbuf  ✓ pango

Tier 4 (toolkit):
  ✓ khronos-headers  ✓ linux-uapi-headers  ✓ libepoxy
  ✓ wayland-host  ✓ wayland-libs  ✓ wayland-protocols  ✓ libxkbcommon
  ✓ gtk3 (3.24 — meson, Wayland-only backend)

Tier 5 (NetSurf libs, custom Makefile):
  ✓ netsurf-buildsystem  ✓ nsgenbind (host build tool)
  ✓ libwapcaplet  ✓ libparserutils  ✓ libhubbub  ✓ libcss
  ✓ libdom  ✓ libnsgif  ✓ libnsbmp  ✓ libnsutils
  ✓ libnslog  ✓ libnspsl  ✓ libsvgtiny

Tier 6 (browser):
  ✓ libpng-host (native libpng for NetSurf's convert_image build tool)
  ✓ netsurf (GTK3 frontend, dynamically linked, /share/netsurf/ resources)
```

## WebKitGTK roadmap

The `webkit` port is intentionally self-contained for now: it stages the
committed `ports/webkit/sysroot` runtime into `${XV6_SYSROOT}` and applies no
network fetches or host package installs. The previous repo-carried WebKitGTK
2.42.5 source overrides have been retired; `apply-xv6-overrides.sh` now exits
successfully when there are no overrides to apply.

Current behavior:

- `port-webkit` validates that the selected runtime has MiniBrowser, the WebKit
  subprocesses, JavaScriptCore/WebKit shared libraries, and the injected bundle.
- The default runtime source is the repo-local `ports/webkit/sysroot`; an
  explicit `XV6_WEBKIT_REF_SYSROOT` or `-DXV6_WEBKIT_REF_SYSROOT=...` can still
  be used for local experiments.
- `XV6_WEBKIT_STRICT_STAGE=ON` is the default, so container builds fail early if
  the runtime payload is incomplete instead of silently producing a blank
  WebKit desktop entry.
- A `.webkit-stage-manifest` is emitted next to the staged executables for
  quick inspection of what landed in the sysroot.

Tier 2 done. The `meson` build mode in `xv6_port()` synthesizes a
cross-file at configure time (CC/AR/strip/ranlib + sysroot +
needs_exe_wrapper=true) and drives `meson setup / compile / install`
with `PKG_CONFIG_SYSROOT_DIR` and `PKG_CONFIG_LIBDIR` pointed at the
sysroot's pkgconfig dirs. Requires meson \u2265 1.6.1 (fontconfig);
installed via `pip install --user --break-system-packages meson` to
get 1.11.1 in `~/.local/bin`.


The `cpython` port now cross-compiles CPython 3.12 from `ports/cpython/src`
(submodule of `ElectronSpark/v6-cpython`, branch `v6-3.12`) against the
from-source ports installed in `${XV6_SYSROOT}` (openssl, sqlite, ncurses,
readline, libffi, zlib, bzip2, xz). It produces `bin/python3.12`,
`lib/libpython3.12.so.1.0`, `lib/python3.12/lib-dynload/*.so` (including
`_ssl`, `_hashlib`, `_ctypes`, `_sqlite3`, `_curses`, `_bz2`, `_lzma`,
`zlib`, `readline`, `select`), and the pure-Python stdlib under
`lib/python3.12/`.

`scripts/stage-cpython.sh` and `CPYTHON_REF_SYSROOT` are no longer used by
this port and can be removed once nothing else references them.

## Replacing the stage step with from-source ports

Build order (DEPENDS chain, leaves first):

```
zlib    bzip2   xz   sqlite   libffi  openssl
                                |        |
                              ncurses    |
                                |        |
                              readline   |
                                +--------+
                                |
                              cpython
```

### Per-port checklist

For each port:

1. **Source provisioning.** Either:
   - `git submodule add <upstream-url> ports/<name>/src`
   - Or symlink `ports/<name>/src` to a local reference checkout for fast iteration.

   Upstream URLs (from `xv6-tmp/.gitmodules`):
   - openssl  → `https://github.com/openssl/openssl.git` (branch `openssl-3.0`)
  - sqlite   → `git@github.com:ElectronSpark/v6-sqlite.git`
  - libffi   → `git@github.com:ElectronSpark/v6-libffi.git`
   - ncurses  → `https://github.com/mirror/ncurses.git`
  - readline → `git@github.com:ElectronSpark/v6-readline.git`
  - cpython  → `git@github.com:ElectronSpark/v6-cpython.git` (branch `3.12`)

   `bzip2`, `xz`, `libuuid` are vendored in `xv6-tmp/user/` (not submodules).

2. **Lift recipe.** Open `xv6-tmp/user/CMakeLists.txt` at the line range above
   and translate the relevant `add_custom_command` block into an `xv6_port()`
   call (see `ports/zlib/CMakeLists.txt` for the pattern).

3. **Cross-compile env.** xv6-tmp's recipes reference variables like
   `MUSL_CRT1_O`, `MUSL_DYNAMIC_LINKER_PATH`, `MUSL_INCLUDE_DIR`,
   `GCC_INCLUDE_DIR_MUSL`, `MUSL_COMPAT_DIR`, `LIBGCC_PATH`,
   `USER_ARCH_CFLAGS`. These are NOT defined in xv6-os yet. Either:

   - **Option A (recommended):** create `cmake/CrossEnv.cmake` that
     derives all of these from `XV6_SYSROOT`, `XV6_TOOLCHAIN_PREFIX`,
     and `XV6_TRIPLE`, then `include()` it from
     `ports/cmake/AddPort.cmake` so every port sees them as plain
     CMake vars.
   - **Option B:** pass each one as `-D` from `cmake/BuildPorts.cmake`
     into the ports sub-cmake invocation.

   Approximate values:
   - `MUSL_DYNAMIC_LINKER_PATH = /lib/ld-musl-x86_64.so.1` (runtime path)
   - `MUSL_CRT1_O              = ${PHASE2_LIB}/crt1.o`
   - `MUSL_INCLUDE_DIR         = ${PHASE2_LIB}/../include`
   - `LIBGCC_PATH              = $(${CMAKE_C_COMPILER} -print-libgcc-file-name)`

4. **CPython specifics.** The cpython recipe is the longest (~300 lines in
   xv6-tmp). Key wrinkles:
   - Generates `Modules/Setup.local` from a template, substituting
     `@CURSES_SETUP_LINES@` and `@OPENSSL_SETUP_LINES@`.
   - `configure --host=x86_64-unknown-xv6 --enable-shared --disable-test-modules
     --without-ensurepip --with-system-ffi --with-openssl=${XV6_SYSROOT}`.
   - Post-install: drop `*-312d-*.so` debug variants, install pure-python
     stdlib (`Lib/`), copy musl loader + libc + libgcc_s into sysroot.
   - **pip-installed packages** (flask, sqlalchemy, ...) must be added by
     a separate post-install step using host pip with `--target` and
     `--no-binary=:all:`. Recommend `ports/cpython/requirements.txt`.

5. **Verify.** After the recipe is in place,
   `cmake --build build --target port-<name>` should populate
   `${XV6_SYSROOT}/lib/<artifact>` and `${XV6_SYSROOT}/include/<header>`.

## Once all ports build from source

- Delete `scripts/stage-cpython.sh` and the `CPYTHON_REF_SYSROOT`
  fallback in `ports/cpython/CMakeLists.txt`.
- Remove `BUILD_ALWAYS 1` in `cmake/BuildPorts.cmake` so ports get
  incremental rebuilds.
- Remove the legacy `initrd` / `image` targets in
  `cmake/BuildImage.cmake`.
