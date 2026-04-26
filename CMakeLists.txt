# ports — heavy third-party software, one CMake project per port.
#
# This top-level CMakeLists is a thin orchestrator. Each port lives
# under <name>/ with its own CMakeLists.txt that calls xv6_port(...).
# Inter-port dependencies are declared inside each port's CMakeLists
# via the DEPENDS argument to xv6_port().
#
# Standalone usage:
#   cmake -S . -B build \
#       -DCMAKE_C_COMPILER=$XV6_TOOLCHAIN_BIN/riscv64-xv6-linux-musl-gcc \
#       -DCMAKE_AR=$XV6_TOOLCHAIN_BIN/riscv64-xv6-linux-musl-ar \
#       -DCMAKE_RANLIB=$XV6_TOOLCHAIN_BIN/riscv64-xv6-linux-musl-ranlib \
#       -DXV6_SYSROOT=$XV6_SYSROOT \
#       -DXV6_PORT_CFLAGS="--sysroot=$XV6_SYSROOT -O2 ..."
#   cmake --build build -j
#
# Each port can also be built independently — its CMakeLists.txt is
# fully standalone (just include AddPort.cmake).

cmake_minimum_required(VERSION 3.16)
project(xv6_ports LANGUAGES NONE)

# Sanity checks.
foreach(req CMAKE_C_COMPILER CMAKE_AR CMAKE_RANLIB)
    if(NOT ${req} OR ${req} STREQUAL "")
        message(FATAL_ERROR "ports/: ${req} is required (cross toolchain).")
    endif()
endforeach()
if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
    message(FATAL_ERROR "ports/: -DXV6_SYSROOT=<path> is required.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------
# Auto-discover ports. Any directory under ports/ that contains a
# CMakeLists.txt is treated as a port; its inter-port dependencies are
# declared inside that CMakeLists via xv6_port(... DEPENDS ...).
# ---------------------------------------------------------------------
file(GLOB _entries RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
     ${CMAKE_CURRENT_SOURCE_DIR}/*)

set(_ports "")
foreach(_e ${_entries})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${_e}
       AND NOT _e STREQUAL "cmake"
       AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_e}/CMakeLists.txt)
        list(APPEND _ports ${_e})
    endif()
endforeach()

list(LENGTH _ports _nports)
message(STATUS "xv6 ports discovered: ${_nports} (${_ports})")

# wayland builds the compositor against the generated wayland-libs output.
list(REMOVE_ITEM _ports wayland)
list(APPEND _ports wayland)

foreach(p IN LISTS _ports)
    add_subdirectory(${p})
endforeach()

add_custom_target(ports-all)
foreach(p IN LISTS _ports)
    add_dependencies(ports-all port-${p})
endforeach()
