cmake_minimum_required(VERSION 3.16)
project(xv6-libudev C)

add_library(udev SHARED libudev.c)
add_library(udev_static STATIC libudev.c)
set_target_properties(udev PROPERTIES
    OUTPUT_NAME udev
    VERSION 1.0.0
    SOVERSION 1)
target_link_options(udev PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libudev.map")
set_target_properties(udev_static PROPERTIES OUTPUT_NAME udev)
target_include_directories(udev PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_include_directories(udev_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)

install(TARGETS udev udev_static
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)
install(FILES libudev.h DESTINATION include)

configure_file(libudev.pc.in libudev.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libudev.pc
    DESTINATION lib/pkgconfig)
