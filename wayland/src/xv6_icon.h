#ifndef XV6_ICON_H
#define XV6_ICON_H

#include <stdint.h>

#define XV6_ICON_SECTION ".xv6.icon"
#define XV6_ICON_MAGIC 0x31493658u
#define XV6_ICON_PALETTE_MAX 16
#define XV6_ICON_MAX_DIM 32
#define XV6_ICON_MAX_PIXELS (XV6_ICON_MAX_DIM * XV6_ICON_MAX_DIM)

struct xv6_icon_blob_header {
    uint32_t magic;
    uint16_t width;
    uint16_t height;
    uint16_t palette_count;
    uint16_t reserved;
    uint32_t palette[XV6_ICON_PALETTE_MAX];
};

struct xv6_icon {
    int width;
    int height;
    uint32_t pixels[XV6_ICON_MAX_PIXELS];
};

int xv6_icon_load_from_elf(const char *path, struct xv6_icon *icon);
void xv6_icon_draw(uint32_t *fb, int fb_w, int fb_h, int x, int y,
                   int w, int h, const struct xv6_icon *icon);

#endif
