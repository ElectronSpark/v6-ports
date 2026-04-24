# xv6-os vim port — from-source cross build of Vim 9.x.
#
# Builds Vim against the cross toolchain's built-in musl sysroot plus our
# from-source ncurses (in ${XV6_SYSROOT}). Installs the binary, xxd, and
# a minimal runtime tree (syntax/colors/indent/autoload + a few core
# .vim files) into ${XV6_SYSROOT}.
#
# Outputs:
#   bin/vim
#   bin/xxd
#   share/vim/vim91/{defaults,filetype,ftoff,indent,indoff,scripts,synmenu}.vim
#   share/vim/vim91/{syntax,colors,indent,autoload}/

cmake_minimum_required(VERSION 3.16)
project(xv6-port-vim LANGUAGES NONE)

if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
    message(FATAL_ERROR "ports/vim: -DXV6_SYSROOT=<path> required")
endif()
if(NOT DEFINED CMAKE_C_COMPILER OR CMAKE_C_COMPILER STREQUAL "")
    message(FATAL_ERROR "ports/vim: -DCMAKE_C_COMPILER=<cross-gcc> required")
endif()

get_filename_component(_default_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(XV6_REPO_ROOT "${_default_repo_root}"
    CACHE PATH "xv6-os repo root (for scripts/patch_vim_exit.sh)")

set(_src "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(NOT EXISTS "${_src}/src/configure")
    message(FATAL_ERROR
        "ports/vim: src/src/configure missing — run\n"
        "  git submodule update --init --recursive ports/vim/src")
endif()

set(_patch_script "${XV6_REPO_ROOT}/scripts/patch_vim_exit.sh")
if(NOT EXISTS "${_patch_script}")
    message(FATAL_ERROR "ports/vim: missing ${_patch_script}")
endif()

# Cross naming
get_filename_component(_cc_realpath "${CMAKE_C_COMPILER}" REALPATH)
get_filename_component(_cc_dir       "${_cc_realpath}"     DIRECTORY)
get_filename_component(_cc_name      "${_cc_realpath}"     NAME)
string(REGEX REPLACE "gcc$" ""  _xv6_prefix "${_cc_name}")
set(_cross_cc      "${_cc_dir}/${_xv6_prefix}gcc")
set(_cross_ar      "${_cc_dir}/${_xv6_prefix}ar")
set(_cross_ranlib  "${_cc_dir}/${_xv6_prefix}ranlib")
set(_cross_strip   "${_cc_dir}/${_xv6_prefix}strip")

string(REGEX REPLACE "-$" "" _xv6_triple "${_xv6_prefix}")
if(_xv6_triple MATCHES "^x86_64")
    set(_vim_host  "x86_64-unknown-linux-musl")
    set(_uname_m   "x86_64")
elseif(_xv6_triple MATCHES "^riscv64")
    set(_vim_host  "riscv64-unknown-linux-musl")
    set(_uname_m   "riscv64")
else()
    message(FATAL_ERROR "ports/vim: unsupported triple ${_xv6_triple}")
endif()

include(ProcessorCount)
if(NOT DEFINED XV6_PARALLEL_JOBS OR XV6_PARALLEL_JOBS EQUAL 0)
    ProcessorCount(XV6_PARALLEL_JOBS)
    if(XV6_PARALLEL_JOBS EQUAL 0)
        set(XV6_PARALLEL_JOBS 4)
    endif()
endif()

# Out-of-tree build: copy src/ to build/ (vim's autoconf needs in-tree).
set(_build "${CMAKE_CURRENT_BINARY_DIR}/build")
set(_copy_stamp "${CMAKE_CURRENT_BINARY_DIR}/.copy_stamp")
add_custom_command(
    OUTPUT  ${_copy_stamp}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND sh -c "cp -a ${_src} ${_build}"
    COMMAND ${CMAKE_COMMAND} -E touch ${_copy_stamp}
    DEPENDS ${_src}/src/configure
    COMMENT "[vim] copy source -> build dir")

set(_vim_src_build "${_build}/src")
set(_config_status "${_vim_src_build}/auto/config.status")

set(_cflags "-O2 -fPIC -I${XV6_SYSROOT}/include")
set(_ldflags "-L${XV6_SYSROOT}/lib -Wl,-rpath,/lib")

add_custom_command(
    OUTPUT  ${_config_status}
    WORKING_DIRECTORY ${_vim_src_build}
    COMMAND ${CMAKE_COMMAND} -E env
                CC=${_cross_cc}
                AR=${_cross_ar}
                RANLIB=${_cross_ranlib}
                STRIP=${_cross_strip}
                "CFLAGS=${_cflags}"
                "LDFLAGS=${_ldflags}"
                "LIBS=-lncurses"
            ./configure
                --host=${_vim_host}
                --prefix=/
                --with-features=normal
                --with-tlib=ncurses
                --disable-gui
                --without-x
                --disable-xim
                --disable-gpm
                --disable-sysmouse
                --disable-nls
                --disable-selinux
                --disable-smack
                --disable-canberra
                --disable-libsodium
                --disable-netbeans
                --disable-channel
                --disable-perlinterp
                --disable-pythoninterp
                --disable-python3interp
                --disable-rubyinterp
                --disable-luainterp
                --disable-tclinterp
                --enable-multibyte
                vim_cv_toupper_broken=no
                vim_cv_terminfo=yes
                vim_cv_tgetent=zero
                vim_cv_getcwd_broken=no
                vim_cv_stat_ignores_slash=no
                vim_cv_memmove_handles_overlap=yes
                vim_cv_memcpy_handles_overlap=yes
                vim_cv_bcopy_handles_overlap=yes
                vim_cv_ipv4_networking=no
                vim_cv_ipv6_networking=no
                vim_cv_timer_create=no
                vim_cv_timer_create_with_lrt=no
                vim_cv_uname_output=Linux
                vim_cv_uname_m_output=${_uname_m}
                vim_cv_uname_r_output=6.0.0
                ac_cv_sizeof_int=4
                ac_cv_sizeof_long=8
                ac_cv_sizeof_off_t=8
                ac_cv_func_getpwuid=yes
                ac_cv_func_getpwnam=yes
                ac_cv_func_getpwent=no
                ac_cv_func_iswupper=yes
                ac_cv_func_sigaction=yes
                ac_cv_func_sigvec=no
                ac_cv_func_sigset=no
                ac_cv_func_sigsetjmp=yes
                ac_cv_func_setpgid=yes
                ac_cv_func_setsid=yes
                ac_cv_func_setjmp=yes
                ac_cv_func_fchdir=yes
                ac_cv_func_fchown=yes
                ac_cv_func_fchmod=yes
                ac_cv_func_fsync=yes
                ac_cv_func_ftruncate=yes
                ac_cv_func_getcwd=yes
                ac_cv_func_getrlimit=yes
                ac_cv_func_gettimeofday=yes
                ac_cv_func_localtime_r=yes
                ac_cv_func_lstat=yes
                ac_cv_func_memset=yes
                ac_cv_func_mkdtemp=yes
                ac_cv_func_nanosleep=yes
                ac_cv_func_opendir=yes
                ac_cv_func_putenv=yes
                ac_cv_func_qsort=yes
                ac_cv_func_readlink=yes
                ac_cv_func_rename=yes
                ac_cv_func_select=yes
                ac_cv_func_setenv=yes
                ac_cv_func_sigaltstack=yes
                ac_cv_func_strftime=yes
                ac_cv_func_stricmp=no
                ac_cv_func_strncasecmp=yes
                ac_cv_func_sysconf=yes
                ac_cv_func_sysinfo=no
                ac_cv_func_towlower=yes
                ac_cv_func_towupper=yes
                ac_cv_func_usleep=yes
    DEPENDS ${_copy_stamp}
    COMMENT "[vim] configure (cross, host=${_vim_host})"
    VERBATIM)

# Build: generate osdef.h, scrub conflicting decls, patch mch_exit, then make.
set(_vim_bin "${_vim_src_build}/vim")
add_custom_command(
    OUTPUT  ${_vim_bin}
    WORKING_DIRECTORY ${_vim_src_build}
    COMMAND make auto/osdef.h
    COMMAND sed -i.bak
        -e "/extern.*poll(/d"
        -e "/struct pollfd/d"
        -e "/extern.*tgetent/d"
        -e "/extern.*tgetnum/d"
        -e "/extern.*tgetflag/d"
        -e "/extern.*tgoto/d"
        -e "/extern.*tputs/d"
        -e "/extern.*open(/d"
        ${_vim_src_build}/auto/osdef.h
    COMMAND ${_patch_script} ${_vim_src_build}/os_unix.c
    COMMAND make -j${XV6_PARALLEL_JOBS} vim
    DEPENDS ${_config_status}
    COMMENT "[vim] build (j=${XV6_PARALLEL_JOBS})"
    VERBATIM)

# xxd built separately (vim's Makefile doesn't pass full LIBS to it).
set(_xxd_bin "${_vim_src_build}/xxd/xxd")
add_custom_command(
    OUTPUT  ${_xxd_bin}
    COMMAND ${_cross_cc}
            -O2 -fPIC -DUNIX
            -L${XV6_SYSROOT}/lib -Wl,-rpath,/lib
            -o ${_xxd_bin}
            ${_vim_src_build}/xxd/xxd.c
    DEPENDS ${_copy_stamp}
    COMMENT "[vim] build xxd"
    VERBATIM)

# Install
set(_runtime_src "${_src}/runtime")
set(_runtime_dst "${XV6_SYSROOT}/share/vim/vim91")
set(_install_stamp "${CMAKE_CURRENT_BINARY_DIR}/.installed")
add_custom_command(
    OUTPUT  ${_install_stamp}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_vim_bin} ${XV6_SYSROOT}/bin/vim
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_xxd_bin} ${XV6_SYSROOT}/bin/xxd
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_runtime_dst}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_runtime_src}/defaults.vim  ${_runtime_dst}/defaults.vim
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_runtime_src}/filetype.vim  ${_runtime_dst}/filetype.vim
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_runtime_src}/ftoff.vim     ${_runtime_dst}/ftoff.vim
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_runtime_src}/indent.vim    ${_runtime_dst}/indent.vim
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_runtime_src}/indoff.vim    ${_runtime_dst}/indoff.vim
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_runtime_src}/scripts.vim   ${_runtime_dst}/scripts.vim
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_runtime_src}/synmenu.vim   ${_runtime_dst}/synmenu.vim
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_runtime_src}/syntax   ${_runtime_dst}/syntax
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_runtime_src}/colors   ${_runtime_dst}/colors
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_runtime_src}/indent   ${_runtime_dst}/indent
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_runtime_src}/autoload ${_runtime_dst}/autoload
    COMMAND ${CMAKE_COMMAND} -E touch ${_install_stamp}
    DEPENDS ${_vim_bin} ${_xxd_bin}
    COMMENT "[vim] install -> ${XV6_SYSROOT}"
    VERBATIM)

# Inter-port dep on ncurses (needs libncurses.a + curses.h + ncurses.h symlink)
add_custom_target(port-vim ALL DEPENDS ${_install_stamp})
add_dependencies(port-vim port-ncurses)

add_custom_target(port-vim-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build}
    COMMAND ${CMAKE_COMMAND} -E rm -f
                ${_copy_stamp} ${_install_stamp}
                ${XV6_SYSROOT}/bin/vim ${XV6_SYSROOT}/bin/xxd
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_runtime_dst}
    COMMENT "[vim] clean")
