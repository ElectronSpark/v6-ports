if(NOT DEFINED OUTPUT OR OUTPUT STREQUAL "")
    message(FATAL_ERROR "write-setup-local.cmake: OUTPUT is required")
endif()
if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
    message(FATAL_ERROR "write-setup-local.cmake: XV6_SYSROOT is required")
endif()

file(WRITE "${OUTPUT}" "*shared*\n")
file(APPEND "${OUTPUT}"
    "_curses _cursesmodule.c -I${XV6_SYSROOT}/include -L${XV6_SYSROOT}/lib -lncurses\n")
file(APPEND "${OUTPUT}"
    "_ssl _ssl.c -I${XV6_SYSROOT}/include -L${XV6_SYSROOT}/lib -lssl -lcrypto\n")
file(APPEND "${OUTPUT}"
    "_hashlib _hashopenssl.c -I${XV6_SYSROOT}/include -L${XV6_SYSROOT}/lib -lcrypto\n")
