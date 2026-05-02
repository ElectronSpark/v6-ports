#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gbm.h>
#include <xf86drm.h>

static int probe_drm_devices(void)
{
    drmDevicePtr devices[8] = { 0 };
    int count = drmGetDevices2(0, devices, 8);

    printf("drmgpuprobe: drmGetDevices2=%d\n", count);
    if (count <= 0)
        return 1;

    for (int i = 0; i < count; i++) {
        if (!devices[i])
            continue;
        printf("drmgpuprobe: device[%d] nodes=0x%x bus=%d\n",
               i, devices[i]->available_nodes, devices[i]->bustype);
        if (devices[i]->available_nodes & (1 << DRM_NODE_RENDER))
            printf("drmgpuprobe: render=%s\n", devices[i]->nodes[DRM_NODE_RENDER]);
    }

    drmFreeDevices(devices, count);
    return 0;
}

static int probe_gbm(int fd)
{
    struct gbm_device *dev;
    struct gbm_bo *bo;
    int prime_fd;

    dev = gbm_create_device(fd);
    if (!dev) {
        printf("drmgpuprobe: gbm_create_device failed: %s\n", strerror(errno));
        return 1;
    }

    printf("drmgpuprobe: gbm backend=%s fd=%d\n",
           gbm_device_get_backend_name(dev), gbm_device_get_fd(dev));

    bo = gbm_bo_create(dev, 64, 64, GBM_FORMAT_XRGB8888,
                       GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR |
                       GBM_BO_USE_WRITE);
    if (!bo) {
        printf("drmgpuprobe: gbm_bo_create failed: %s\n", strerror(errno));
        gbm_device_destroy(dev);
        return 1;
    }

    prime_fd = gbm_bo_get_fd(bo);
    printf("drmgpuprobe: bo %ux%u stride=%u fd=%d\n",
           gbm_bo_get_width(bo), gbm_bo_get_height(bo),
           gbm_bo_get_stride(bo), prime_fd);
    if (prime_fd >= 0)
        close(prime_fd);

    gbm_bo_destroy(bo);
    gbm_device_destroy(dev);
    return prime_fd < 0;
}

int main(void)
{
    struct stat st;
    int failed;
    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);

    if (stat("/dev/dri/renderD128", &st) == 0)
        printf("drmgpuprobe: render stat mode=0%o rdev=%llu\n",
               (unsigned)st.st_mode, (unsigned long long)st.st_rdev);
    else
        printf("drmgpuprobe: render stat failed: %s\n", strerror(errno));

    failed = probe_drm_devices();

    if (fd < 0) {
        printf("drmgpuprobe: open render node failed: %s\n", strerror(errno));
        return 1;
    }

    printf("drmgpuprobe: fd node_type=%d\n", drmGetNodeTypeFromFd(fd));

    failed |= probe_gbm(fd);
    close(fd);

    if (failed)
        return 1;
    printf("drmgpuprobe: passed\n");
    return 0;
}
