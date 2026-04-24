cmake_minimum_required(VERSION 3.16)
project(xv6-port-curl LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            curl
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    cmake
    DEPENDS         zlib openssl
    OUTPUT_FILES    lib/libcurl.a
                    include/curl/curl.h
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=/
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_CURL_EXE=OFF
        -DBUILD_TESTING=OFF
        -DCURL_USE_OPENSSL=ON
        -DOPENSSL_CRYPTO_LIBRARY=${XV6_SYSROOT}/lib/libcrypto.a
        -DOPENSSL_SSL_LIBRARY=${XV6_SYSROOT}/lib/libssl.a
        -DOPENSSL_INCLUDE_DIR=${XV6_SYSROOT}/include
        -DZLIB_LIBRARY=${XV6_SYSROOT}/lib/libz.a
        -DZLIB_INCLUDE_DIR=${XV6_SYSROOT}/include
        -DPKG_CONFIG_EXECUTABLE=/bin/false
        -DCURL_DISABLE_LDAP=ON
        -DCURL_DISABLE_LDAPS=ON
        -DHTTP_ONLY=ON
        -DCURL_USE_LIBPSL=OFF
        -DCURL_USE_LIBSSH2=OFF
        -DCURL_USE_LIBSSH=OFF
        -DENABLE_UNIX_SOCKETS=OFF
        -DUSE_THREADS_POSIX=ON
        -DCURL_CA_BUNDLE=none
        -DCURL_CA_PATH=none
)
