/*
 * mesad3d12probe.c - force Mesa's Gallium D3D12 path for Hyper-V bring-up.
 *
 * The xv6 shell only recently gained VAR=value command-prefix handling, and
 * validator scripts still need stable serial output while Hyper-V OpenGL
 * support is being wired up. Keep the D3D12 Mesa environment inside this small
 * wrapper so a missing shell feature cannot silently fall back to softpipe.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void report_path(const char *label, const char *path)
{
    int rc = access(path, F_OK);

    printf("mesad3d12probe: %s path=%s present=%d errno=%d\n",
           label, path, rc == 0, rc == 0 ? 0 : errno);
}

int main(int argc, char **argv_in)
{
    char *const argv[] = { "mesaglfeature", NULL };
    const char *adapter = NULL;
    int fault_trace = 0;
    int full_trace = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv_in[i], "--fault-trace") == 0) {
            fault_trace = 1;
            continue;
        }
        if (strcmp(argv_in[i], "--full-trace") == 0) {
            fault_trace = 1;
            full_trace = 1;
            continue;
        }
        if (strcmp(argv_in[i], "--adapter") == 0 && i + 1 < argc) {
            adapter = argv_in[++i];
            continue;
        }
        if (strncmp(argv_in[i], "--adapter=", 10) == 0) {
            adapter = argv_in[i] + 10;
            continue;
        }
        fprintf(stderr, "mesad3d12probe: unknown argument: %s\n",
                argv_in[i]);
        return 2;
    }

    report_path("dri", "/lib/dri/d3d12_dri.so");
    report_path("dri_multiarch", "/usr/lib/x86_64-linux-gnu/dri/d3d12_dri.so");
    report_path("dxcore", "/lib/libdxcore.so");
    report_path("dxcore_multiarch", "/usr/lib/x86_64-linux-gnu/libdxcore.so");
    report_path("d3d12", "/lib/libd3d12.so");
    report_path("d3d12_multiarch", "/usr/lib/x86_64-linux-gnu/libd3d12.so");

    setenv("GALLIUM_DRIVER", "d3d12", 1);
    setenv("MESA_LOADER_DRIVER_OVERRIDE", "d3d12", 1);
    setenv("LIBGL_ALWAYS_SOFTWARE", "0", 1);
    setenv("LIBGL_DRIVERS_PATH",
           "/lib/dri:/usr/lib/x86_64-linux-gnu/dri", 1);
    setenv("LD_LIBRARY_PATH",
           "/lib:/usr/lib:/usr/lib/x86_64-linux-gnu", 1);
    setenv("XV6_MESAGLFEATURE_REQUIRE_D3D12", "1", 1);
    setenv("MESA_DEBUG", "1", 1);
    setenv("LIBGL_DEBUG", "verbose", 1);
    setenv("D3D12_DEBUG", "verbose", 1);
    if (adapter && adapter[0])
        setenv("MESA_D3D12_DEFAULT_ADAPTER_NAME", adapter, 1);
    if (fault_trace) {
        setenv("LD_PRELOAD", "/lib/xv6_glibc_fault_trace.so", 1);
        if (!full_trace)
            setenv("XV6_DXG_TRACE_SUBMIT_ONLY", "1", 1);
        else
            setenv("XV6_DXG_TRACE_COMPACT", "1", 1);
    }
    printf("mesad3d12probe: exec mesaglfeature with GALLIUM_DRIVER=d3d12 MESA_LOADER_DRIVER_OVERRIDE=d3d12 D3D12_DEBUG=verbose LIBGL_DRIVERS_PATH=%s LD_LIBRARY_PATH=%s adapter=%s fault_trace=%d full_trace=%d\n",
           getenv("LIBGL_DRIVERS_PATH"), getenv("LD_LIBRARY_PATH"),
           adapter ? adapter : "(default)", fault_trace, full_trace);
    fflush(stdout);

    execvp(argv[0], argv);
    fprintf(stderr, "mesad3d12probe: exec mesaglfeature failed: %s\n",
            strerror(errno));
    return 127;
}
