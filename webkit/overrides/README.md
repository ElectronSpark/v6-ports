# WebKitGTK xv6 Source Overrides

This directory is intentionally empty.

The xv6 port no longer carries WebKitGTK 2.42.5 source override files, and
`ports/webkit/apply-xv6-overrides.sh` is now a no-op validator. New WebKit work
should prefer fixing xv6 kernel, driver, Wayland, libc, or port-runtime behavior
instead of patching WebKitGTK source.

If a future clean upstream WebKitGTK rebuild exposes an unavoidable compatibility
delta, document the kernel-side gap first and keep any temporary patch series out
of this directory until it is intentionally reviewed.
