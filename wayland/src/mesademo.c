#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define XV6_DRM_RENDER_NODE "/dev/dri/renderD128"
#define FB_GPU_BACKEND_QUERY 0x462C
#define FB_GPU_BACKEND_HYPERV_DXG 2
#define FB_GPU_BACKEND_F_DXG_TRANSPORT 0x0008
#define FB_GPU_BACKEND_F_OPENGL_SUBMIT 0x0020

struct fb_gpu_backend_info_compat {
    uint32_t backend;
    uint32_t flags;
    uint32_t capset_id;
    uint32_t capset_version;
    uint32_t capset_size;
    uint32_t dxg_global_open;
    uint32_t dxg_vgpu_open;
    uint32_t dxg_d3dkmt;
    uint32_t dxg_global_status;
    uint32_t dxg_vgpu_status;
    uint32_t dxg_global_rx;
    uint32_t dxg_vgpu_rx;
    char name[32];
    char renderer[64];
};

static int query_backend(struct fb_gpu_backend_info_compat *info)
{
    int fd;
    int ok;

    memset(info, 0, sizeof(*info));
    fd = open(XV6_DRM_RENDER_NODE, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return 0;
    ok = ioctl(fd, FB_GPU_BACKEND_QUERY, info) == 0;
    close(fd);
    return ok;
}

static void set_default_env(const char *key, const char *value)
{
    if (!getenv(key))
        setenv(key, value, 0);
}

static int pass_through_arg(const char *arg)
{
    return strcmp(arg, "--perf-log") == 0 ||
           strcmp(arg, "--no-inplace") == 0 ||
           strcmp(arg, "--sync-frontbuffer") == 0 ||
           strcmp(arg, "--no-front-flush") == 0 ||
           strcmp(arg, "--direct-backbuffer") == 0 ||
           strncmp(arg, "--frames=", 9) == 0 ||
           strncmp(arg, "--loops=", 8) == 0 ||
           strncmp(arg, "--size=", 7) == 0 ||
           strncmp(arg, "--resize-every=", 15) == 0 ||
           strncmp(arg, "--pace-us=", 10) == 0 ||
           strncmp(arg, "--present-interval=", 19) == 0 ||
           strncmp(arg, "--render-div=", 13) == 0;
}

int main(int argc, char **in_argv)
{
    struct fb_gpu_backend_info_compat info;
    int have_info = query_backend(&info);
    char *argv[16];
    int out_argc = 0;

    argv[out_argc++] = "mesawlegl";
    argv[out_argc++] = "--demo";

    set_default_env("HOME", "/");
    set_default_env("PATH", "/bin:/usr/bin");
    set_default_env("XDG_RUNTIME_DIR", "/tmp");
    set_default_env("XDG_CACHE_HOME", "/tmp/.cache");
    set_default_env("XDG_DATA_DIRS", "/share:/usr/share");
    set_default_env("WAYLAND_DISPLAY", "wayland-0");
    set_default_env("GDK_BACKEND", "wayland");
    set_default_env("XCURSOR_PATH", "/share/icons");
    set_default_env("XCURSOR_THEME", "Adwaita");
    set_default_env("EGL_PLATFORM", "wayland");
    set_default_env("LIBGL_DRIVERS_PATH", "/lib/dri");
    set_default_env("vblank_mode", "0");

    if (have_info && info.backend == FB_GPU_BACKEND_HYPERV_DXG &&
        (info.flags & FB_GPU_BACKEND_F_DXG_TRANSPORT) != 0) {
        setenv("LIBGL_ALWAYS_SOFTWARE", "0", 1);
        setenv("GALLIUM_DRIVER", "d3d12", 1);
        setenv("XV6_MESA_WAYLAND_THROTTLE", "0", 1);
        setenv("XV6_MESA_WAYLAND_XV6GPU", "1", 1);
        setenv("XV6_MESA_WAYLAND_INPLACE_PRESENT", "1", 1);
        setenv("XV6_D3D12_PRESENT_INTERVAL", "1", 1);
    } else if (have_info &&
               (info.flags & FB_GPU_BACKEND_F_OPENGL_SUBMIT) != 0) {
        setenv("LIBGL_ALWAYS_SOFTWARE", "0", 1);
        setenv("GALLIUM_DRIVER", "virgl", 1);
    } else {
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        setenv("MESA_LOADER_DRIVER_OVERRIDE", "swrast", 1);
    }

    for (int i = 1; i < argc && out_argc < (int)(sizeof(argv) / sizeof(argv[0])) - 1; i++) {
        if (strcmp(in_argv[i], "--native-drm") == 0)
            setenv("XV6_MESA_WAYLAND_DXG_DRM_PROBE", "1", 1);
        else if (pass_through_arg(in_argv[i]))
            argv[out_argc++] = in_argv[i];
        else
            fprintf(stderr, "mesademo: ignoring unsupported option '%s'\n",
                    in_argv[i]);
    }
    argv[out_argc] = NULL;

    execv("/bin/mesawlegl", argv);
    perror("mesademo: exec /bin/mesawlegl");
    return 127;
}
