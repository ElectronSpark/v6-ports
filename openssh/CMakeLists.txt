cmake_minimum_required(VERSION 3.16)
project(xv6-port-openssh LANGUAGES NONE)

if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
    message(FATAL_ERROR "Port openssh: -DXV6_SYSROOT=<path> required")
endif()
foreach(v CMAKE_C_COMPILER CMAKE_AR CMAKE_RANLIB)
    if(NOT ${v} OR ${v} STREQUAL "")
        message(FATAL_ERROR "Port openssh: ${v} is not set.")
    endif()
endforeach()

set(_src   ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(_build ${CMAKE_BINARY_DIR}/openssh-build)

find_program(_strip NAMES strip REQUIRED)

if(NOT EXISTS "${_src}/configure")
    message(FATAL_ERROR "Port openssh: missing source submodule at ${_src}")
endif()

if(NOT DEFINED XV6_PORT_CFLAGS OR XV6_PORT_CFLAGS STREQUAL "")
    set(XV6_PORT_CFLAGS "-O2 -fPIC")
endif()

if(NOT DEFINED XV6_PARALLEL_JOBS OR XV6_PARALLEL_JOBS STREQUAL "")
    include(ProcessorCount)
    ProcessorCount(XV6_PARALLEL_JOBS)
    if(XV6_PARALLEL_JOBS EQUAL 0)
        set(XV6_PARALLEL_JOBS 4)
    endif()
endif()

set(_configure_stamp ${_build}/config.status)
set(_build_stamp     ${_build}/xv6-openssh-built.stamp)

set(_install_bins
    ssh
    sshd
    ssh-keygen
    ssh-keyscan
    scp
    sftp)

set(_installed "")
foreach(_bin IN LISTS _install_bins)
    list(APPEND _installed "${XV6_SYSROOT}/bin/${_bin}")
endforeach()
list(APPEND _installed "${XV6_SYSROOT}/libexec/sshd-session")

add_custom_command(
    OUTPUT ${_configure_stamp}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_src} ${_build}
    COMMAND ${CMAKE_COMMAND} -E touch ${_build}/configure
    COMMAND ${CMAKE_COMMAND} -E chdir ${_build}
            ${CMAKE_COMMAND} -E env
                CC=${CMAKE_C_COMPILER}
                AR=${CMAKE_AR}
                RANLIB=${CMAKE_RANLIB}
                STRIP=${_strip}
                "CFLAGS=${XV6_PORT_CFLAGS} -D_GNU_SOURCE -DHAVE_ATTRIBUTE__SENTINEL__=1"
                "CPPFLAGS=-I${XV6_SYSROOT}/include"
                "LDFLAGS=-L${XV6_SYSROOT}/lib"
                "LIBS=-lcrypto"
            ./configure
                --prefix=/
                --sbindir=/bin
                --libexecdir=/libexec
                --sysconfdir=/etc/ssh
                --with-privsep-path=/var/empty
                --with-privsep-user=sshd
                --with-ssl-dir=${XV6_SYSROOT}
                --without-pie
                --without-pam
                --without-selinux
                --without-libedit
                --without-zlib
                --without-openssl-header-check
                --disable-strip
                --disable-utmp
                --disable-utmpx
                --disable-wtmp
                --disable-wtmpx
                --disable-lastlog
                --disable-pkcs11
                --disable-security-key
                ac_cv_func_arc4random=no
                ac_cv_func_arc4random_buf=no
                ac_cv_func_arc4random_stir=no
                ac_cv_func_arc4random_uniform=no
                ac_cv_func_closefrom=no
                ac_cv_func_endgrent=yes
                ac_cv_func_err=yes
                ac_cv_func_errx=yes
                ac_cv_func_explicit_bzero=yes
                ac_cv_func_fchmod=yes
                ac_cv_func_fchown=yes
                ac_cv_func_freeaddrinfo=yes
                ac_cv_func_fstatfs=yes
                ac_cv_func_futimes=yes
                ac_cv_func_getaddrinfo=yes
                ac_cv_func_getcwd=yes
                ac_cv_func_getgrouplist=yes
                ac_cv_func_getgrset=no
                ac_cv_func_getlastlogxbyname=no
                ac_cv_func_getnameinfo=yes
                ac_cv_func_getopt=yes
                ac_cv_func_getpeereid=no
                ac_cv_func_getpeerucred=no
                ac_cv_func_getrlimit=yes
                ac_cv_func_glob=yes
                ac_cv_func_group_from_gid=no
                ac_cv_func_inet_aton=yes
                ac_cv_func_inet_ntoa=yes
                ac_cv_func_inet_ntop=yes
                ac_cv_func_login_getcapbool=no
                ac_cv_func_mblen=yes
                ac_cv_func_memmove=yes
                ac_cv_func_mkdtemp=yes
                ac_cv_func_ngetaddrinfo=no
                ac_cv_func_openpty=yes
                ac_cv_func_poll=yes
                ac_cv_func_prctl=yes
                ac_cv_func_pselect=yes
                ac_cv_func_readpassphrase=no
                ac_cv_func_realpath=yes
                ac_cv_func_recvmsg=yes
                ac_cv_func_rresvport_af=no
                ac_cv_func_sendmsg=yes
                ac_cv_func_setenv=yes
                ac_cv_func_setgroupent=no
                ac_cv_func_setgroups=yes
                ac_cv_func_setlinebuf=yes
                ac_cv_func_setlogin=no
                ac_cv_func_setpassent=no
                ac_cv_func_setpcred=no
                ac_cv_func_setproctitle=no
                ac_cv_func_setregid=yes
                ac_cv_func_setresgid=yes
                ac_cv_func_setresuid=yes
                ac_cv_func_setreuid=yes
                ac_cv_func_setsid=yes
                ac_cv_func_sigaction=yes
                ac_cv_func_sigvec=no
                ac_cv_func_snprintf=yes
                ac_cv_func_socketpair=yes
                ac_cv_func_statfs=yes
                ac_cv_func_statvfs=yes
                ac_cv_func_fstatvfs=yes
                ac_cv_func_strftime=yes
                ac_cv_func_strlcat=yes
                ac_cv_func_strlcpy=yes
                ac_cv_func_strptime=yes
                ac_cv_func_strsep=yes
                ac_cv_func_strtoll=yes
                ac_cv_func_strtonum=no
                ac_cv_func_strtoul=yes
                ac_cv_func_swap32=no
                ac_cv_func_sysconf=yes
                ac_cv_func_tcgetpgrp=yes
                ac_cv_func_truncate=yes
                ac_cv_func_unsetenv=yes
                ac_cv_func_updwtmpx=no
                ac_cv_func_user_from_uid=no
                ac_cv_func_usleep=yes
                ac_cv_func_vasprintf=yes
                ac_cv_func_vsnprintf=yes
                ac_cv_func_waitpid=yes
                ac_cv_func_warn=yes
                ac_cv_have_control_in_msghdr=yes
                ac_cv_have_broken_snprintf=no
                ac_cv_header_endian_h=yes
                ac_cv_header_features_h=yes
                ac_cv_header_floatingpoint_h=no
                ac_cv_header_ifaddrs_h=no
                ac_cv_header_lastlog_h=no
                ac_cv_header_linux_if_tun_h=no
                ac_cv_header_login_h=no
                ac_cv_header_maillock_h=no
                ac_cv_header_ndir_h=no
                ac_cv_header_net_if_tun_h=no
                ac_cv_header_poll_h=yes
                ac_cv_header_pty_h=yes
                ac_cv_header_readpassphrase_h=no
                ac_cv_header_rpc_types_h=no
                ac_cv_header_security_pam_appl_h=no
                ac_cv_header_shadow_h=yes
                ac_cv_header_sys_dir_h=no
                ac_cv_header_sys_mount_h=no
                ac_cv_header_sys_ndir_h=no
                ac_cv_header_sys_poll_h=yes
                ac_cv_header_sys_ptms_h=no
                ac_cv_header_sys_select_h=yes
                ac_cv_header_sys_statvfs_h=yes
                ac_cv_header_sys_stropts_h=no
                ac_cv_header_sys_sysmacros_h=yes
                ac_cv_header_sys_time_h=yes
                ac_cv_header_sys_un_h=yes
                ac_cv_header_util_h=no
                ac_cv_header_utmp_h=yes
                ac_cv_header_utmpx_h=no
                ac_cv_header_vis_h=no
                ac_cv_lib_crypt_crypt=yes
                "ac_cv_search_crypt=none required"
                ac_cv_search_dlopen=no
                ac_cv_sizeof_short=2
                ac_cv_sizeof_int=4
                ac_cv_sizeof_long=8
                ac_cv_sizeof_long_long=8
    DEPENDS port-openssl
    COMMENT "Configuring port openssh"
    VERBATIM)

add_custom_command(
    OUTPUT ${_build_stamp}
    COMMAND ${CMAKE_COMMAND} -E chdir ${_build} make -j${XV6_PARALLEL_JOBS}
    COMMAND ${CMAKE_COMMAND} -E touch ${_build_stamp}
    DEPENDS ${_configure_stamp}
    COMMENT "Building port openssh"
    VERBATIM)

add_custom_command(
    OUTPUT ${_installed}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/bin ${XV6_SYSROOT}/libexec
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_build}/ssh ${XV6_SYSROOT}/bin/ssh
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_build}/sshd ${XV6_SYSROOT}/bin/sshd
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_build}/ssh-keygen ${XV6_SYSROOT}/bin/ssh-keygen
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_build}/ssh-keyscan ${XV6_SYSROOT}/bin/ssh-keyscan
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_build}/scp ${XV6_SYSROOT}/bin/scp
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_build}/sftp ${XV6_SYSROOT}/bin/sftp
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_build}/sshd-session ${XV6_SYSROOT}/libexec/sshd-session
    DEPENDS ${_build_stamp}
    COMMENT "Installing port openssh"
    VERBATIM)

add_custom_target(port-openssh ALL DEPENDS ${_installed})
add_dependencies(port-openssh port-openssl)

add_custom_target(port-openssh-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${_installed}
    COMMENT "Removing openssh build artifacts")
