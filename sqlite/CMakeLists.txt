# xv6-os sqlite port — STUB.
#
# Submodule (https://github.com/ElectronSpark/v6-sqlite.git) is in place.
# Upstream build is the SQLite amalgamation (sqlite3.c) compiled with a
# tailored set of -DSQLITE_* flags. The full recipe lives in
# xv6-tmp/user/CMakeLists.txt around line 1094.
#
# This stub registers `port-sqlite` as a noop so the umbrella's
# auto-discovery doesn't break. Replace the body with a real xv6_port()
# call to wire the build (see ports/MIGRATION.md).

cmake_minimum_required(VERSION 3.16)
project(xv6-port-sqlite LANGUAGES NONE)

add_custom_target(port-sqlite ALL
    COMMAND ${CMAKE_COMMAND} -E echo "[port-sqlite] STUB - see ports/MIGRATION.md")
