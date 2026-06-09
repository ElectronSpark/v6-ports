cmake_minimum_required(VERSION 3.16)
project(xv6-hwdata NONE)

install(FILES pnp.ids DESTINATION share/hwdata)
configure_file(hwdata.pc.in hwdata.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/hwdata.pc
    DESTINATION share/pkgconfig)
