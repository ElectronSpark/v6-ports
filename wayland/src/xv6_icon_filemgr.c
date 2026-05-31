#include "xv6_icon.h"

struct xv6_icon_blob_16 {
    struct xv6_icon_blob_header header;
    unsigned char pixels[16 * 16];
};

__attribute__((used, section(XV6_ICON_SECTION)))
const struct xv6_icon_blob_16 xv6_filemgr_icon = {
    .header = {
        .magic = XV6_ICON_MAGIC,
        .width = 16,
        .height = 16,
        .palette_count = 5,
        .reserved = 0,
        .palette = {
            0x00000000,
            0xFF6F4E1F,
            0xFFA67C3D,
            0xFFD9B45F,
            0xFFF2D98B,
        },
    },
    .pixels =
        "0000000000000000"
        "0001111000000000"
        "0013333111110000"
        "0133333333331000"
        "1233333333332100"
        "1233333333332100"
        "1234444444432100"
        "1234444444432100"
        "1234444444432100"
        "1234444444432100"
        "1233333333332100"
        "1233333333332100"
        "1222222222222100"
        "0111111111111000"
        "0000000000000000"
        "0000000000000000",
};
