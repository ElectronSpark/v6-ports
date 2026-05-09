cmake_minimum_required(VERSION 3.16)
project(xv6-port-netsurf LANGUAGES NONE)

# NetSurf browser — GTK3 frontend, built with the host compiler/glibc against
# the from-source ports under ${XV6_SYSROOT}.
#
# Unlike the smaller NetSurf libraries this port cannot use
# AddPort.cmake's BUILD_SYSTEM=netsurf mode because its build needs:
#   - a pkg-config wrapper that rewrites -I/include and -L/lib coming
#     from sysroot .pc files (which use prefix=/) to absolute sysroot
#     paths, while leaving netsurf-prefix paths alone;
#   - host libpng on PATH/LDFLAGS for build-tool convert_image;
#   - host nsgenbind, glib-compile-resources and wayland-scanner on PATH;
#   - a Makefile.config tailored to NETSURF_USE_* knobs.
#
# All of that is encapsulated in a single shell script driven by a
# CMake custom_command. The generated binary lands at
# ${XV6_SYSROOT}/bin/netsurf and resources at
# ${XV6_SYSROOT}/share/netsurf/.

set(NS_SRC          ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(NS_SHARED       ${CMAKE_CURRENT_SOURCE_DIR}/../netsurf-buildsystem/src)
set(NS_BUILD        ${CMAKE_BINARY_DIR}/netsurf-build)
set(NS_OBJROOT      ${NS_BUILD}/Linux-gtk3)
set(NS_HOST_TOOLS   ${XV6_SYSROOT}/host-tools)
set(NS_HOST_PNG_LIB ${NS_HOST_TOOLS}/lib)
set(NS_HOST_PNG_INC ${NS_HOST_TOOLS}/include)
set(NS_BIN          ${XV6_SYSROOT}/bin/netsurf)
set(NS_SHARE        ${XV6_SYSROOT}/share/netsurf)
set(NS_RES          ${NS_SRC}/frontends/gtk/res)
set(NS_BUNDLED_FONTS ${CMAKE_CURRENT_SOURCE_DIR}/../pango/src/tests/fonts)
set(NS_GLYPH_DATA   ${NS_SRC}/frontends/framebuffer/res/fonts/glyph_data)

set(NS_CC           ${CMAKE_C_COMPILER})
set(NS_AR           ${CMAKE_AR})
set(NS_RANLIB       ${CMAKE_RANLIB})

# libgcc.a — query the cross compiler.
execute_process(
    COMMAND ${NS_CC} -print-libgcc-file-name
    OUTPUT_VARIABLE NS_LIBGCC OUTPUT_STRIP_TRAILING_WHITESPACE)

set(NS_BUILD_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/build-netsurf.sh)

# ----------------------------------------------------------------------
# Build script (generated at configure time, executed at build time).
# ----------------------------------------------------------------------
file(WRITE ${NS_BUILD_SCRIPT} "#!/bin/bash
set -e

NS_SRC=\"${NS_SRC}\"
SYSROOT=\"${XV6_SYSROOT}\"
NS_BUILD=\"${NS_BUILD}\"
NS_OBJROOT=\"${NS_OBJROOT}\"
NS_SHARED=\"${NS_SHARED}\"
NS_HOST_TOOLS=\"${NS_HOST_TOOLS}\"
NS_HOST_PNG_LIB=\"${NS_HOST_PNG_LIB}\"
NS_HOST_PNG_INC=\"${NS_HOST_PNG_INC}\"
NS_BUNDLED_FONTS=\"${NS_BUNDLED_FONTS}\"
NS_GLYPH_DATA=\"${NS_GLYPH_DATA}\"
LIBGCC=\"${NS_LIBGCC}\"
REAL_CC=\"${NS_CC}\"

mkdir -p \"\${NS_BUILD}\" \"\${NS_OBJROOT}\" \"\${NS_HOST_TOOLS}/bin\"

# Host-glibc CFLAGS. Add sysroot include/lib paths for inter-port headers and
# libraries (gtk/glib/cairo/etc.) while leaving libc as the native host glibc.
export CC=\"\${NS_BUILD}/cc-wrapper\"
export AR=\"${NS_AR}\"
export RANLIB=\"${NS_RANLIB}\"
export CFLAGS=\"-O2 -fPIC -DHAVE_CTIME_R=1 -isystem \${SYSROOT}/include\"
export CXXFLAGS=\"\${CFLAGS}\"
export LDFLAGS=\"-L\${SYSROOT}/lib -Wl,-rpath,/lib -Wl,-rpath-link,\${SYSROOT}/lib\"

# pkg-config wiring (talks to .pc files under sysroot)
export PKG_CONFIG=\"\${NS_BUILD}/pkg-config-wrapper\"
export PKG_CONFIG_SYSROOT_DIR=\"\${SYSROOT}\"
export PKG_CONFIG_LIBDIR=\"\${SYSROOT}/lib/pkgconfig:\${SYSROOT}/share/pkgconfig\"
export GDK_PIXBUF_PIXDATA=\"/usr/bin/gdk-pixbuf-pixdata\"
export PATH=\"\${NS_HOST_TOOLS}/bin:\${PATH}\"

# --- CC wrapper: keep one place for future link shims, but use normal glibc ---
cat > \"\${CC}\" << CCEOF
#!/bin/bash
exec \"\${REAL_CC}\" \"\\\$@\"
CCEOF
chmod +x \"\${CC}\"

# --- pkg-config wrapper: --static for --libs, sysroot-rewrite -I/-L ---
cat > \"\${PKG_CONFIG}\" << PCEOF
#!/bin/bash
export PKG_CONFIG_SYSROOT_DIR=\"\${PKG_CONFIG_SYSROOT_DIR}\"
export PKG_CONFIG_LIBDIR=\"\${PKG_CONFIG_LIBDIR}\"
args=(\"\\\$@\")
for a in \"\\\$@\"; do
  case \"\\\$a\" in --libs) args+=(--static); break;; esac
done
result=\\\$(/usr/bin/pkg-config \"\\\${args[@]}\"); rc=\\\$?
[ -n \"\\\$result\" ] && echo \"\\\$result\" | sed -e 's|-I/include|-I\${SYSROOT}/include|g' -e 's|-I/lib|-I\${SYSROOT}/lib|g' -e 's|-L/lib|-L\${SYSROOT}/lib|g'
exit \\\$rc
PCEOF
chmod +x \"\${PKG_CONFIG}\"

cd \"\${NS_SRC}\"

python3 - <<'PY'
from pathlib import Path
p = Path(\"frontends/gtk/gui.c\")
s = p.read_text()
s = s.replace('strdup(\"Sans\")', 'strdup(\"DejaVu Sans\")')
s = s.replace('strdup(\"Serif\")', 'strdup(\"DejaVu Sans\")')
s = s.replace('strdup(\"Monospace\")', 'strdup(\"DejaVu Sans Mono\")')
p.write_text(s)
PY

# Makefile.config — frontend selection + USE_* knobs
cat > Makefile.config << EOF
override PKGCONFIG := \${PKG_CONFIG}
override NETSURF_USE_JPEG := YES
override NETSURF_USE_PNG := YES
override NETSURF_USE_GIF := YES
override NETSURF_USE_BMP := YES
override NETSURF_USE_WEBP := NO
override NETSURF_USE_NSSVG := YES
override NETSURF_USE_ROSPRITE := NO
override NETSURF_USE_HARU_PDF := NO
override NETSURF_USE_NSLOG := YES
override NETSURF_USE_NSPSL := YES
override NETSURF_USE_DUKTAPE := YES
override NETSURF_USE_CURL := YES
override NETSURF_USE_OPENSSL := YES
override NETSURF_USE_UTF8PROC := NO
override NETSURF_USE_RSVG := NO
override NETSURF_USE_VIDEO := NO
override NETSURF_USE_GRESOURCE := YES
override NETSURF_USE_INLINE_PIXBUF := NO
override NETSURF_GTK_MAJOR := 3
EOF

rm -rf \"\${NS_OBJROOT}\"
mkdir -p \"\${NS_OBJROOT}\"
make clean TARGET=gtk3 NSSHARED=\"\${NS_SHARED}\" OBJROOT=\"\${NS_OBJROOT}\" 2>/dev/null || true

make -j\$(nproc) TARGET=gtk3 \\
    NSSHARED=\"\${NS_SHARED}\" \\
    OBJROOT=\"\${NS_OBJROOT}\" \\
    PREFIX=/ \\
    PKGCONFIG=\"\${PKG_CONFIG}\" \\
    PKG_CONFIG=\"\${PKG_CONFIG}\" \\
    BUILD_CFLAGS=\"-I\${NS_HOST_PNG_INC}\" \\
    BUILD_LDFLAGS=\"-L\${NS_HOST_PNG_LIB}\" \\
    BUILD_LIBPNG_LDFLAGS=\"-lpng -lz -lm\" \\
    GLIB_COMPILE_RESOURCES=\"\${NS_HOST_TOOLS}/bin/glib-compile-resources\" \\
    GDK_PIXBUF_PIXDATA=\"\${GDK_PIXBUF_PIXDATA}\" \\
    NETSURF_GTK_RES_PATH=\"/share/netsurf/\" \\
    Q=

# Locate output binary (varies between in-tree and build/<target>/ layouts)
BIN=\"\"
for c in \"\${NS_OBJROOT}/nsgtk3\" build/Linux-gtk3/nsgtk3 nsgtk3 frontends/gtk/nsgtk3; do
  [ -f \"\$c\" ] && BIN=\"\$c\" && break
done
[ -n \"\$BIN\" ] || { echo 'ERROR: nsgtk3 binary not found'; find . -name 'nsgtk*' -type f; exit 1; }

mkdir -p \"\${SYSROOT}/bin\" \"\${SYSROOT}/share/netsurf\"
install -m 0755 \"\$BIN\" \"\${SYSROOT}/bin/netsurf\"

# ---- Resources ----
NS_RES=\"\${NS_SRC}/frontends/gtk/res\"
NS_SHARE=\"\${SYSROOT}/share/netsurf\"
FC_FONT_DIR=\"\${SYSROOT}/usr/share/fonts/truetype/dejavu\"
ALT_FONT_DIR=\"\${SYSROOT}/share/fonts/dejavu\"
mkdir -p \"\${NS_SHARE}/icons\" \"\${NS_SHARE}/throbber\" \"\${NS_SHARE}/gtk3\" \"\${NS_SHARE}/fonts\" \"\${FC_FONT_DIR}\" \"\${ALT_FONT_DIR}\"
cp -f \"\${NS_RES}\"/*.css \"\${NS_RES}\"/*.html \"\${NS_RES}\"/*.png \"\${NS_SHARE}/\" 2>/dev/null || true
cp -f \"\${NS_RES}\"/*.ico \"\${NS_RES}\"/*.xpm \"\${NS_SHARE}/\" 2>/dev/null || true
cp -f \"\${NS_RES}/SearchEngines\" \"\${NS_RES}/Messages\" \"\${NS_RES}/accelerators\" \"\${NS_SHARE}/\" 2>/dev/null || true
[ -f \"\${NS_SRC}/resources/ca-bundle\" ] && cp -f \"\${NS_SRC}/resources/ca-bundle\" \"\${NS_SHARE}/ca-bundle\"
cat > \"\${NS_SHARE}/welcome.html\" << 'WELCOME_EOF'
<!doctype html>
<html>
<head>
<meta charset=\"utf-8\">
<title>Welcome to NetSurf</title>
<style>
html,body{margin:0;padding:0;background:#fff;color:#111;font-family:sans-serif}
.banner{background:#94adff;padding:16px 20px}
.banner img{display:block;width:308px;height:86px}
main{max-width:760px;margin:28px auto;padding:0 24px;line-height:1.45}
.panel{background:#eef3ff;border:2px solid #c7d5ff;padding:16px;margin-top:20px}
code{background:#f5f5f5;padding:1px 4px}
</style>
</head>
<body>
<div class=\"banner\"><img src=\"about:logo\" alt=\"NetSurf\"></div>
<main>
<h1>Welcome to NetSurf</h1>
<p>This xv6 build is using a local start page so opening the browser does not immediately depend on external networking.</p>
<div class=\"panel\">
<p>Try local URLs such as <code>file:///share/netsurf/welcome.html</code>, or enter a network URL manually when you want to test the TCP/IP path.</p>
</div>
</main>
</body>
</html>
WELCOME_EOF
cp -rf \"\${NS_RES}/icons/.\"   \"\${NS_SHARE}/icons/\"   2>/dev/null || true
cp -rf \"\${NS_RES}/throbber/.\" \"\${NS_SHARE}/throbber/\" 2>/dev/null || true
cp -f  \"\${NS_RES}/gtk3/\"*.ui \"\${NS_SHARE}/gtk3/\" 2>/dev/null || true
for lang in de en fr it ja nl zh_CN; do
  if [ -d \"\${NS_RES}/\${lang}\" ]; then
    mkdir -p \"\${NS_SHARE}/\${lang}\"
    cp -rf \"\${NS_RES}/\${lang}/.\" \"\${NS_SHARE}/\${lang}/\"
  fi
done
DEJAVU=/usr/share/fonts/truetype/dejavu
copy_font_file() {
  local src=\"\$1\"
  [ -f \"\$src\" ] || return 0
  cp -f \"\$src\" \"\${NS_SHARE}/fonts/\"
  cp -f \"\$src\" \"\${FC_FONT_DIR}/\"
  cp -f \"\$src\" \"\${ALT_FONT_DIR}/\"
}

if [ -d \"\${NS_BUNDLED_FONTS}\" ]; then
  for f in DejaVuSans.ttf DejaVuSans-Bold.ttf DejaVuSansMono.ttf; do
    copy_font_file \"\${NS_BUNDLED_FONTS}/\$f\"
  done
fi
if [ -f \"\${NS_GLYPH_DATA}\" ]; then
  cp -f \"\${NS_GLYPH_DATA}\" \"\${NS_SHARE}/fonts/glyph_data\"
fi
if [ -d \"\$DEJAVU\" ]; then
  for f in DejaVuSans.ttf DejaVuSans-Bold.ttf DejaVuSans-Oblique.ttf \\
           DejaVuSans-BoldOblique.ttf DejaVuSerif.ttf DejaVuSerif-Bold.ttf \\
           DejaVuSansMono.ttf DejaVuSansMono-Bold.ttf; do
    copy_font_file \"\$DEJAVU/\$f\"
  done
fi

echo '=== NetSurf installed: '\"\${SYSROOT}/bin/netsurf\"' ==='
")

# ----------------------------------------------------------------------
# Hook the script into CMake's dependency graph.
# ----------------------------------------------------------------------
add_custom_command(
    OUTPUT  ${NS_BIN}
    COMMAND bash ${NS_BUILD_SCRIPT}
    DEPENDS ${NS_BUILD_SCRIPT}
    COMMENT "Building NetSurf (GTK3) browser"
    VERBATIM)

add_custom_target(port-netsurf ALL DEPENDS ${NS_BIN})

# Inter-port build ordering (other ports' targets are added by the
# umbrella discovery loop).
set(_ns_deps
    netsurf-buildsystem netsurf-nsgenbind libpng-host curl openssl
    netsurf-libwapcaplet netsurf-libparserutils netsurf-libhubbub
    netsurf-libcss netsurf-libdom netsurf-libnsgif netsurf-libnsbmp
    netsurf-libnsutils netsurf-libnslog netsurf-libnspsl netsurf-libsvgtiny
    gtk3 glib-host)
foreach(d IN LISTS _ns_deps)
    add_dependencies(port-netsurf port-${d})
endforeach()

add_custom_target(port-netsurf-clean
    COMMAND make -C ${NS_SRC} clean TARGET=gtk3 NSSHARED=${NS_SHARED} || true
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${NS_BUILD}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${NS_BIN})
