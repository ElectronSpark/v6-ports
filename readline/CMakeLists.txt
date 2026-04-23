# xv6-os readline port — STUB.
#
# Submodule (https://git.savannah.gnu.org/git/readline.git) is in place.
# Upstream build is autoconf, depends on ncurses (see
# xv6-tmp/user/CMakeLists.txt around line 478).
#
# Stub target so umbrella discovery succeeds; flesh out per
# ports/MIGRATION.md.

cmake_minimum_required(VERSION 3.16)
project(xv6-port-readline LANGUAGES NONE)

add_custom_target(port-readline ALL
    COMMAND ${CMAKE_COMMAND} -E echo "[port-readline] STUB - see ports/MIGRATION.md")
