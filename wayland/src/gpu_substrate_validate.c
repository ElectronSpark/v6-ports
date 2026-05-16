#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#define FB_GPU_BACKEND_QUERY 0x462C
#define FB_GPU_BACKEND_DUMB 0
#define FB_GPU_BACKEND_VIRGL 1
#define FB_GPU_BACKEND_HYPERV_DXG 2
#define FB_GPU_BACKEND_F_OPENGL_SUBMIT 0x0020

struct fb_gpu_backend_info {
    unsigned int backend;
    unsigned int flags;
    unsigned int capset_id;
    unsigned int capset_version;
    unsigned int capset_size;
    unsigned int dxg_global_open;
    unsigned int dxg_vgpu_open;
    unsigned int dxg_d3dkmt;
    unsigned int dxg_global_status;
    unsigned int dxg_vgpu_status;
    unsigned int dxg_global_rx;
    unsigned int dxg_vgpu_rx;
    char name[32];
    char renderer[64];
};

struct probe_step {
    const char *done_marker;
    int requires_opengl_submit;
    char *const *argv;
};

static const char *backend_name(unsigned int backend)
{
    switch (backend) {
    case FB_GPU_BACKEND_VIRGL:
        return "virgl";
    case FB_GPU_BACKEND_HYPERV_DXG:
        return "hyperv-dxg";
    case FB_GPU_BACKEND_DUMB:
    default:
        return "dumb";
    }
}

static int query_backend(struct fb_gpu_backend_info *info)
{
    int fd = open("/dev/gpu0", O_RDONLY);

    if (fd < 0)
        fd = open("/dev/fb0", O_RDONLY);
    if (fd < 0)
        return -1;

    memset(info, 0, sizeof(*info));
    int rc = ioctl(fd, FB_GPU_BACKEND_QUERY, info);
    close(fd);
    return rc;
}

static int run_step(const struct probe_step *step)
{
    printf("gpu-substrate-validate: run");
    for (int i = 0; step->argv[i]; i++)
        printf(" %s", step->argv[i]);
    printf("\n");
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "gpu-substrate-validate: fork failed: %s\n",
                strerror(errno));
        return 1;
    }

    if (pid == 0) {
        execvp(step->argv[0], step->argv);
        fprintf(stderr, "gpu-substrate-validate: exec %s failed: %s\n",
                step->argv[0], strerror(errno));
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        fprintf(stderr, "gpu-substrate-validate: waitpid %s failed: %s\n",
                step->argv[0], strerror(errno));
        return 1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (WIFSIGNALED(status)) {
            fprintf(stderr, "gpu-substrate-validate: %s killed by signal %d\n",
                    step->argv[0], WTERMSIG(status));
        } else {
            fprintf(stderr, "gpu-substrate-validate: %s exited status %d\n",
                    step->argv[0],
                    WIFEXITED(status) ? WEXITSTATUS(status) : status);
        }
        return 1;
    }

    printf("%s\n", step->done_marker);
    fflush(stdout);
    return 0;
}

int main(void)
{
    char *gbmtest[] = { "gbmtest", NULL };
    char *dmabufsmoke[] = { "dmabufsmoke", NULL };
    char *mesawlegl4[] = { "mesawlegl", "--frames=4", "--loops=1",
                           "--resize-every=2", NULL };
    char *mesawlegl6[] = { "mesawlegl", "--frames=6", "--loops=1",
                           "--resize-every=3", NULL };
    char *mesaglsmoke[] = { "mesaglsmoke", "--frames=6", "--loops=1",
                            "--resize-every=3", NULL };
    char *virgltest[] = { "virgltest", NULL };
    char *virgl_async[] = { "virgltest", "--async-submit", NULL };
    char *virgl_invalid[] = { "virgltest", "--invalid-submit", NULL };
    char *virgl_bad[] = { "virgltest", "--bad-submit", NULL };
    char *mouseinject[] = { "mouseinject", "65535", "65535", NULL };
    char *gpubuf3[] = { "gpubuftest", "3", NULL };
    char *gpubuf_owner[] = { "gpubuftest", "--render-owner", NULL };
    char *fbstat[] = { "fbstat", NULL };

    const struct probe_step steps[] = {
        { "__GPUV_GBM_DONE_0__", 0, gbmtest },
        { "__GPUV_DMABUF_DONE_0__", 1, dmabufsmoke },
        { "__GPUV_MESAWLEGL4_DONE_0__", 1, mesawlegl4 },
        { "__GPUV_MESAWLEGL6_DONE_0__", 1, mesawlegl6 },
        { "__GPUV_MESAGL_DONE_0__", 1, mesaglsmoke },
        { "__GPUV_VIRGL_DONE_0__", 1, virgltest },
        { "__GPUV_VIRGL_ASYNC_DONE_0__", 1, virgl_async },
        { "__GPUV_VIRGL_INVALID_DONE_0__", 1, virgl_invalid },
        { "__GPUV_VIRGL_BAD_DONE_0__", 1, virgl_bad },
        { "__GPUV_MOUSE_DONE_0__", 0, mouseinject },
        { "__GPUV_GPUBUF3_DONE_0__", 0, gpubuf3 },
        { "__GPUV_GPUBUFOWNER_DONE_0__", 0, gpubuf_owner },
        { "__GPUV_FBSTAT_DONE_0__", 0, fbstat },
    };
    struct fb_gpu_backend_info backend;
    int have_backend = query_backend(&backend) == 0;
    int opengl_submit = 1;
    const char *force = getenv("GPUV_FORCE_OPENGL");

    setenv("PATH", "/bin:/usr/bin", 1);
    setenv("XDG_RUNTIME_DIR", "/tmp", 1);
    setenv("WAYLAND_DISPLAY", "wayland-0", 1);
    setenv("EGL_PLATFORM", "wayland", 1);
    setenv("LIBGL_ALWAYS_SOFTWARE", "0", 1);
    setenv("GALLIUM_DRIVER", "virgl", 1);

    if (have_backend) {
        opengl_submit =
            (backend.flags & FB_GPU_BACKEND_F_OPENGL_SUBMIT) != 0;
        printf("gpu-substrate-validate: backend=%s flags=0x%x renderer=%s opengl_submit=%d\n",
               backend.name[0] ? backend.name : backend_name(backend.backend),
               backend.flags,
               backend.renderer[0] ? backend.renderer : "unknown",
               opengl_submit);
    } else {
        printf("gpu-substrate-validate: backend query unavailable; running all probes\n");
    }

    if (force && strcmp(force, "0") != 0) {
        printf("gpu-substrate-validate: GPUV_FORCE_OPENGL=%s; running OpenGL-submit probes\n",
               force);
        opengl_submit = 1;
    }

    printf("__GPUV_READY__\n");
    fflush(stdout);

    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        if (steps[i].requires_opengl_submit && !opengl_submit) {
            printf("gpu-substrate-validate: skip %s: backend has no OPENGL_SUBMIT\n",
                   steps[i].argv[0]);
            continue;
        }
        if (i == sizeof(steps) / sizeof(steps[0]) - 1)
            sleep(1);
        if (run_step(&steps[i]) != 0)
            return 1;
    }

    return 0;
}
