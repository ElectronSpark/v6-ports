# ports — heavy third-party software, one CMake project per port.

Migration and source-cleanup work is tracked in the umbrella repository’s
[consolidated plan](../docs/active-work-plan.md#upstream-source-cleanup-and-port-migration).
The [original ports roadmap](../docs/archive/plan-consolidation-20260907/ports/MIGRATION.md)
is preserved for reference.

Each port wraps its upstream native build via `ExternalProject_Add`
(through the `xv6_port()` helper). Inter-port deps are explicit:

```
xv6_port(NAME libpng ... DEPENDS zlib)
```

Add a port:

1. Create `ports/<name>/CMakeLists.txt` calling `xv6_port(...)`.
2. Add `<name>` to the `PORTS` list in `ports/CMakeLists.txt`.
3. Tarballs go to a shared cache (`XV6_DOWNLOAD_DIR`); same hash means
   no redownload across arches.

Standalone build (without the umbrella):

```sh
cmake -S . -B build \
      -DCMAKE_TOOLCHAIN_FILE=/path/to/cross.cmake \
      -DXV6_SYSROOT=/path/to/sysroot \
      -DCMAKE_INSTALL_PREFIX=/path/to/sysroot/usr
cmake --build build -j --target ports-all
```
