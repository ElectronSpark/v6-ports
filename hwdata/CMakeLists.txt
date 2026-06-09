cmake_minimum_required(VERSION 3.16)
project(xv6-port-hwdata LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            hwdata
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    cmake
    OUTPUT_FILES    host-tools/share/hwdata/pnp.ids
                    host-tools/share/pkgconfig/hwdata.pc
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=/host-tools
)
