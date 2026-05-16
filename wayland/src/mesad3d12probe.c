/*
 * mesad3d12probe.c - force Mesa's Gallium D3D12 path for Hyper-V bring-up.
 *
 * The xv6 shell does not implement VAR=value command prefixes, and this probe
 * needs stable serial output while Hyper-V OpenGL support is being wired up.
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
    int fault_trace = argc > 1 &&
        (strcmp(argv_in[1], "--fault-trace") == 0 ||
         strcmp(argv_in[1], "--full-trace") == 0);
    int full_trace = argc > 1 && strcmp(argv_in[1], "--full-trace") == 0;

    report_path("dri", "/lib/dri/d3d12_dri.so");
    report_path("dri_multiarch", "/usr/lib/x86_64-linux-gnu/dri/d3d12_dri.so");
    report_path("dxcore", "/lib/libdxcore.so");
    report_path("dxcore_multiarch", "/usr/lib/x86_64-linux-gnu/libdxcore.so");
    report_path("d3d12", "/lib/libd3d12.so");
    report_path("d3d12_multiarch", "/usr/lib/x86_64-linux-gnu/libd3d12.so");

    setenv("GALLIUM_DRIVER", "d3d12", 1);
    setenv("MESA_DEBUG", "1", 1);
    setenv("LIBGL_DEBUG", "verbose", 1);
    setenv("D3D12_DEBUG", "verbose", 1);
    if (fault_trace) {
        setenv("LD_PRELOAD", "/lib/xv6_glibc_fault_trace.so", 1);
        if (!full_trace)
            setenv("XV6_DXG_TRACE_SUBMIT_ONLY", "1", 1);
        else
            setenv("XV6_DXG_TRACE_COMPACT", "1", 1);
    }
    printf("mesad3d12probe: exec mesaglfeature with GALLIUM_DRIVER=d3d12 D3D12_DEBUG=verbose fault_trace=%d full_trace=%d\n",
           fault_trace, full_trace);
    fflush(stdout);

    execvp(argv[0], argv);
    fprintf(stderr, "mesad3d12probe: exec mesaglfeature failed: %s\n",
            strerror(errno));
    return 127;
}
