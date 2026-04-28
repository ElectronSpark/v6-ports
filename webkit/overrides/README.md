# WebKitGTK xv6 Source Overrides

This directory is intentionally empty of active WebKitGTK source overrides.

The previous WebKitGTK 2.42.5 override files were retired from the repo after
the kernel/user reproducer pass and runtime validation.  The supported WebKit
path now stages the repo-local prebuilt runtime from `ports/webkit/sysroot`.

The helper remains available for future source-port experiments and exits
successfully when no overrides are present:

```sh
ports/webkit/apply-xv6-overrides.sh /path/to/webkitgtk-2.42.5
```

If a clean upstream WebKitGTK rebuild exposes missing xv6 behavior, add the fix
as a real in-tree port source change or a narrowly documented patch series
rather than relying on an untracked external checkout.
