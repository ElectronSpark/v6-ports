# WebKitGTK xv6 Source Overrides

This directory records the WebKitGTK 2.42.5 source changes recovered from the
older `/home/es/xv6/xv6-tmp` porting tree.

These are source files, not generated build output. Apply them to a clean
WebKitGTK 2.42.5 source tree with:

```sh
ports/webkit/apply-xv6-overrides.sh /path/to/webkitgtk-2.42.5
```

The overrides preserve the xv6-specific MiniBrowser lifecycle fixes, WebKit IPC
tuning, network-cache controls, shared-memory path, crash tracing, and GTK
runtime knobs that were otherwise only present in the external tree.
