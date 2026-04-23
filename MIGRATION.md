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
| sqlite    | submodule + STUB CMakeLists     | `ports/sqlite/CMakeLists.txt`                  |
| ncurses   | submodule + STUB CMakeLists     | `ports/ncurses/CMakeLists.txt`                 |
| readline  | submodule + STUB CMakeLists     | `ports/readline/CMakeLists.txt`                |
| openssl   | submodule + STUB CMakeLists     | `ports/openssl/CMakeLists.txt`                 |
| cpython   | **stage from prebuilt sysroot** | `ports/cpython/CMakeLists.txt` (wraps stage-cpython.sh) |

The `cpython` port currently wraps `scripts/stage-cpython.sh`, which copies a
known-good prebuilt CPython 3.12 + Flask stack from a reference sysroot
(default: `/home/es/xv6/xv6-tmp/build-x86/sysroot`) into `${XV6_SYSROOT}`.
This is enough to make `cmake --build build --target world` produce a
bootable Python-capable `fs.img` with no manual steps, but it depends on
the reference sysroot being present.

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
   - Or symlink `ports/<name>/src` → `/home/es/xv6/xv6-tmp/user/<name>` for fast iteration.

   Upstream URLs (from `xv6-tmp/.gitmodules`):
   - openssl  → `https://github.com/openssl/openssl.git` (branch `openssl-3.0`)
   - sqlite   → `https://github.com/ElectronSpark/v6-sqlite.git`
   - libffi   → `https://github.com/ElectronSpark/v6-libffi.git`
   - ncurses  → `https://github.com/mirror/ncurses.git`
   - readline → `https://git.savannah.gnu.org/git/readline.git`
   - cpython  → `https://github.com/ElectronSpark/v6-cpython.git` (branch `3.12`)

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
