# xv6-os openssl port — STUB.
#
# Submodule (https://github.com/openssl/openssl.git, branch openssl-3.0)
# is in place. Upstream uses its own perl-based ./Configure script with
# the linux-x86_64 / linux64-riscv64 targets and --cross-compile-prefix
# (see xv6-tmp/user/CMakeLists.txt around line 720).
#
# Stub target so umbrella discovery succeeds; flesh out per
# ports/MIGRATION.md.

cmake_minimum_required(VERSION 3.16)
project(xv6-port-openssl LANGUAGES NONE)

add_custom_target(port-openssl ALL
    COMMAND ${CMAKE_COMMAND} -E echo "[port-openssl] STUB - see ports/MIGRATION.md")
