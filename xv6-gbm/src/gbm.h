#ifndef XV6_GBM_H
#define XV6_GBM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GBM_BO_USE_SCANOUT    (1u << 0)
#define GBM_BO_USE_CURSOR     (1u << 1)
#define GBM_BO_USE_RENDERING  (1u << 2)
#define GBM_BO_USE_WRITE      (1u << 3)
#define GBM_BO_USE_LINEAR     (1u << 4)

#define GBM_BO_TRANSFER_READ       (1u << 0)
#define GBM_BO_TRANSFER_WRITE      (1u << 1)
#define GBM_BO_TRANSFER_READ_WRITE (GBM_BO_TRANSFER_READ | GBM_BO_TRANSFER_WRITE)

#define GBM_FORMAT_ARGB8888 0x34325241u
#define GBM_FORMAT_XRGB8888 0x34325258u

#define GBM_FORMAT_MOD_LINEAR 0ULL
#define GBM_FORMAT_MOD_INVALID ((1ULL << 56) - 1)

#define GBM_BO_IMPORT_FD 0x5501u

struct gbm_device;
struct gbm_bo;
struct gbm_surface;

union gbm_bo_handle {
    void *ptr;
    int32_t s32;
    uint32_t u32;
    int64_t s64;
    uint64_t u64;
};

struct gbm_import_fd_data {
    int fd;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
};

struct gbm_device *gbm_create_device(int fd);
void gbm_device_destroy(struct gbm_device *gbm);
int gbm_device_get_fd(struct gbm_device *gbm);
const char *gbm_device_get_backend_name(struct gbm_device *gbm);
int gbm_device_is_format_supported(struct gbm_device *gbm, uint32_t format,
                                   uint32_t usage);

struct gbm_bo *gbm_bo_create(struct gbm_device *gbm, uint32_t width,
                             uint32_t height, uint32_t format,
                             uint32_t usage);
struct gbm_bo *gbm_bo_create_with_modifiers(struct gbm_device *gbm,
                                            uint32_t width, uint32_t height,
                                            uint32_t format,
                                            const uint64_t *modifiers,
                                            uint32_t count);
struct gbm_bo *gbm_bo_create_with_modifiers2(struct gbm_device *gbm,
                                             uint32_t width, uint32_t height,
                                             uint32_t format,
                                             const uint64_t *modifiers,
                                             uint32_t count, uint32_t flags);
struct gbm_bo *gbm_bo_import(struct gbm_device *gbm, uint32_t type,
                             void *buffer, uint32_t usage);
void gbm_bo_destroy(struct gbm_bo *bo);

struct gbm_device *gbm_bo_get_device(struct gbm_bo *bo);
uint32_t gbm_bo_get_width(struct gbm_bo *bo);
uint32_t gbm_bo_get_height(struct gbm_bo *bo);
uint32_t gbm_bo_get_stride(struct gbm_bo *bo);
uint32_t gbm_bo_get_stride_for_plane(struct gbm_bo *bo, int plane);
uint32_t gbm_bo_get_format(struct gbm_bo *bo);
uint64_t gbm_bo_get_modifier(struct gbm_bo *bo);
int gbm_bo_get_plane_count(struct gbm_bo *bo);
int gbm_bo_get_fd_for_plane(struct gbm_bo *bo, int plane);
uint32_t gbm_bo_get_offset(struct gbm_bo *bo, int plane);
union gbm_bo_handle gbm_bo_get_handle(struct gbm_bo *bo);
union gbm_bo_handle gbm_bo_get_handle_for_plane(struct gbm_bo *bo, int plane);
int gbm_bo_get_fd(struct gbm_bo *bo);
void gbm_bo_set_user_data(struct gbm_bo *bo, void *data,
                          void (*destroy_user_data)(struct gbm_bo *, void *));
void *gbm_bo_get_user_data(struct gbm_bo *bo);
int gbm_bo_write(struct gbm_bo *bo, const void *buf, size_t count);
void *gbm_bo_map(struct gbm_bo *bo, uint32_t x, uint32_t y, uint32_t width,
                 uint32_t height, uint32_t flags, uint32_t *stride,
                 void **map_data);
void gbm_bo_unmap(struct gbm_bo *bo, void *map_data);

struct gbm_surface *gbm_surface_create(struct gbm_device *gbm,
                                       uint32_t width, uint32_t height,
                                       uint32_t format, uint32_t flags);
struct gbm_surface *gbm_surface_create_with_modifiers(
    struct gbm_device *gbm, uint32_t width, uint32_t height, uint32_t format,
    const uint64_t *modifiers, uint32_t count);
struct gbm_surface *gbm_surface_create_with_modifiers2(
    struct gbm_device *gbm, uint32_t width, uint32_t height, uint32_t format,
    const uint64_t *modifiers, uint32_t count, uint32_t flags);
struct gbm_bo *gbm_surface_lock_front_buffer(struct gbm_surface *surface);
void gbm_surface_release_buffer(struct gbm_surface *surface,
                                struct gbm_bo *bo);
int gbm_surface_has_free_buffers(struct gbm_surface *surface);
void gbm_surface_destroy(struct gbm_surface *surface);

#ifdef __cplusplus
}
#endif

#endif
