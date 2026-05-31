#include "xv6_icon.h"

struct xv6_icon_blob_16 {
    struct xv6_icon_blob_header header;
    unsigned char pixels[16 * 16];
};

__attribute__((used, section(XV6_ICON_SECTION)))
const struct xv6_icon_blob_16 xv6_glmaze_icon = {
    .header = {
        .magic = XV6_ICON_MAGIC,
        .width = 16,
        .height = 16,
        .palette_count = 6,
        .reserved = 0,
        .palette = {
            0x00000000,
            0xFF2B3440,
            0xFF6B3F22,
            0xFFA86D35,
            0xFF5B74E8,
            0xFF95A7FF,
        },
    },
    .pixels =
        "1111111111111111"
        "1444444444444441"
        "1441111111111441"
        "1441222222211441"
        "1441223333211441"
        "1441223113211441"
        "1441223113211441"
        "1441223333211441"
        "1441111113211441"
        "1444444413211441"
        "1111111413211441"
        "1233333413211441"
        "1231111113211441"
        "1233333333211441"
        "1111111111111551"
        "1111111111111111",
};
