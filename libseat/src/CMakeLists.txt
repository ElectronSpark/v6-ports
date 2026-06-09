cmake_minimum_required(VERSION 3.16)
project(xv6-libseat C)

add_library(seat SHARED libseat.c)
add_library(seat_static STATIC libseat.c)
set_target_properties(seat PROPERTIES
    OUTPUT_NAME seat
    VERSION 1.0.0
    SOVERSION 1)
set_target_properties(seat_static PROPERTIES OUTPUT_NAME seat)
target_include_directories(seat PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_include_directories(seat_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)

install(TARGETS seat seat_static
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)
install(FILES libseat.h DESTINATION include)

configure_file(libseat.pc.in libseat.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libseat.pc
    DESTINATION lib/pkgconfig)
