cmake_minimum_required(VERSION 3.16)
project(xv6-libevdev C)

add_library(evdev SHARED libevdev.c)
add_library(evdev_static STATIC libevdev.c)
set_target_properties(evdev PROPERTIES
    OUTPUT_NAME evdev
    VERSION 2.0.0
    SOVERSION 2)
target_link_options(evdev PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libevdev.map")
set_target_properties(evdev_static PROPERTIES OUTPUT_NAME evdev)
target_include_directories(evdev PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_include_directories(evdev_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)

install(TARGETS evdev evdev_static
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)
install(FILES libevdev/libevdev.h DESTINATION include/libevdev)

configure_file(libevdev.pc.in libevdev.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libevdev.pc
    DESTINATION lib/pkgconfig)
