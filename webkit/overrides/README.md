# WebKitGTK xv6 Source Overrides

`webkitgtk-2.42.5/` contains the xv6 source overrides needed to reproduce the
repo-staged WebKitGTK runtime from a clean WebKitGTK 2.42.5 tree.

The overrides cover the xv6 IPC/shared-memory assumptions, surfaceless EGL
fallback, WebKit GTK accelerated-compositing guards, and the small runtime
feature gates used by the current Wayland/WebKit smoke tests.  Apply them before
building WebKitGTK for the xv6 sysroot:

```sh
ports/webkit/apply-xv6-overrides.sh /path/to/webkitgtk-2.42.5
```

Keep this directory narrow: do not add temporary diagnostics or build products
here.  If a clean rebuild exposes another xv6 behavior gap, land it as a source
override and refresh the staged runtime.
