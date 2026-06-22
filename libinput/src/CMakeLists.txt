cmake_minimum_required(VERSION 3.16)
project(xv6-libinput C)

add_library(input SHARED libinput.c)
add_library(input_static STATIC libinput.c)
find_library(UDEV_LIBRARY NAMES udev REQUIRED)
set_target_properties(input PROPERTIES
    OUTPUT_NAME input
    VERSION 10.0.0
    SOVERSION 10)
set_target_properties(input_static PROPERTIES OUTPUT_NAME input)
target_include_directories(input PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_include_directories(input_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_link_options(input PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libinput.map")
target_link_libraries(input PRIVATE ${UDEV_LIBRARY})
target_link_libraries(input_static PRIVATE ${UDEV_LIBRARY})

install(TARGETS input input_static
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)
install(FILES libinput.h DESTINATION include)

configure_file(libinput.pc.in libinput.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libinput.pc
    DESTINATION lib/pkgconfig)
