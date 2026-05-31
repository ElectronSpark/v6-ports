#include "xv6_icon.h"

struct xv6_icon_blob_16 {
    struct xv6_icon_blob_header header;
    unsigned char pixels[16 * 16];
};

__attribute__((used, section(XV6_ICON_SECTION)))
const struct xv6_icon_blob_16 xv6_peanutgb_icon = {
    .header = {
        .magic = XV6_ICON_MAGIC,
        .width = 16,
        .height = 16,
        .palette_count = 7,
        .reserved = 0,
        .palette = {
            0x00000000,
            0xFF252A32,
            0xFFB8BEC8,
            0xFF7D8796,
            0xFF9BBC0F,
            0xFF306230,
            0xFFB33A3A,
        },
    },
    .pixels =
        "0001111111110000"
        "0012222222221000"
        "0122222222222100"
        "0124444444432100"
        "0124555555432100"
        "0124554455432100"
        "0124555555432100"
        "0124444444432100"
        "0122222222222100"
        "0122312221322100"
        "0123112222132100"
        "0122222222222100"
        "0122262222622100"
        "0122222262222100"
        "0012222222221000"
        "0001111111110000",
};
