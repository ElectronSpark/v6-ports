# xv6-os ncurses port — STUB.
#
# Submodule (https://github.com/mirror/ncurses.git) is in place.
# Upstream build is autoconf with extensive --without-* options
# (see xv6-tmp/user/CMakeLists.txt around line 357).
#
# Stub target so umbrella discovery succeeds; flesh out per
# ports/MIGRATION.md.

cmake_minimum_required(VERSION 3.16)
project(xv6-port-ncurses LANGUAGES NONE)

add_custom_target(port-ncurses ALL
    COMMAND ${CMAKE_COMMAND} -E echo "[port-ncurses] STUB - see ports/MIGRATION.md")
