#include "xv6_icon.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wlcomp_draw.h"

static int read_exact_at(int fd, void *buf, size_t len, off_t off)
{
    unsigned char *p = buf;
    size_t done = 0;

    while (done < len) {
        ssize_t n = pread(fd, p + done, len - done, off + (off_t)done);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        done += (size_t)n;
    }
    return 0;
}

static int icon_index_from_byte(unsigned char v)
{
    if (v >= '0' && v <= '9')
        return v - '0';
    if (v >= 'A' && v <= 'F')
        return 10 + v - 'A';
    if (v >= 'a' && v <= 'f')
        return 10 + v - 'a';
    if (v < XV6_ICON_PALETTE_MAX)
        return v;
    return 0;
}

static int load_icon_section(int fd, const Elf64_Shdr *sh,
                             struct xv6_icon *icon)
{
    struct xv6_icon_blob_header hdr;
    unsigned char encoded[XV6_ICON_MAX_PIXELS];
    size_t pixel_count;

    if (sh->sh_size < sizeof(hdr))
        return -1;
    if (read_exact_at(fd, &hdr, sizeof(hdr), (off_t)sh->sh_offset) != 0)
        return -1;
    if (hdr.magic != XV6_ICON_MAGIC || hdr.width == 0 || hdr.height == 0 ||
        hdr.width > XV6_ICON_MAX_DIM || hdr.height > XV6_ICON_MAX_DIM ||
        hdr.palette_count == 0 ||
        hdr.palette_count > XV6_ICON_PALETTE_MAX)
        return -1;

    pixel_count = (size_t)hdr.width * (size_t)hdr.height;
    if (sh->sh_size < sizeof(hdr) + pixel_count)
        return -1;
    if (read_exact_at(fd, encoded, pixel_count,
                      (off_t)(sh->sh_offset + sizeof(hdr))) != 0)
        return -1;

    memset(icon, 0, sizeof(*icon));
    icon->width = hdr.width;
    icon->height = hdr.height;
    for (size_t i = 0; i < pixel_count; i++) {
        int idx = icon_index_from_byte(encoded[i]);

        icon->pixels[i] = idx < hdr.palette_count ? hdr.palette[idx] : 0;
    }
    return 0;
}

int xv6_icon_load_from_elf(const char *path, struct xv6_icon *icon)
{
    int fd;
    Elf64_Ehdr eh;
    Elf64_Shdr *shdrs = NULL;
    char *shstr = NULL;
    int rc = -1;

    if (!path || !path[0] || !icon)
        return -1;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    if (read_exact_at(fd, &eh, sizeof(eh), 0) != 0)
        goto out;
    if (memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_ident[EI_CLASS] != ELFCLASS64 ||
        eh.e_ident[EI_DATA] != ELFDATA2LSB ||
        eh.e_shentsize != sizeof(Elf64_Shdr) ||
        eh.e_shnum == 0 || eh.e_shnum > 4096 ||
        eh.e_shstrndx == SHN_UNDEF || eh.e_shstrndx >= eh.e_shnum)
        goto out;

    shdrs = calloc((size_t)eh.e_shnum, sizeof(*shdrs));
    if (!shdrs)
        goto out;
    if (read_exact_at(fd, shdrs, (size_t)eh.e_shnum * sizeof(*shdrs),
                      (off_t)eh.e_shoff) != 0)
        goto out;
    if (shdrs[eh.e_shstrndx].sh_size == 0 ||
        shdrs[eh.e_shstrndx].sh_size > 65536)
        goto out;
    shstr = malloc((size_t)shdrs[eh.e_shstrndx].sh_size);
    if (!shstr)
        goto out;
    if (read_exact_at(fd, shstr, (size_t)shdrs[eh.e_shstrndx].sh_size,
                      (off_t)shdrs[eh.e_shstrndx].sh_offset) != 0)
        goto out;

    for (int i = 0; i < eh.e_shnum; i++) {
        const char *name;

        if (shdrs[i].sh_name >= shdrs[eh.e_shstrndx].sh_size)
            continue;
        name = shstr + shdrs[i].sh_name;
        if (strcmp(name, XV6_ICON_SECTION) == 0 &&
            load_icon_section(fd, &shdrs[i], icon) == 0) {
            rc = 0;
            break;
        }
    }

out:
    free(shstr);
    free(shdrs);
    close(fd);
    return rc;
}

static uint32_t blend_argb(uint32_t dst, uint32_t src)
{
    uint32_t a = src >> 24;
    uint32_t inv = 255 - a;
    uint32_t sr = (src >> 16) & 0xff;
    uint32_t sg = (src >> 8) & 0xff;
    uint32_t sb = src & 0xff;
    uint32_t dr = (dst >> 16) & 0xff;
    uint32_t dg = (dst >> 8) & 0xff;
    uint32_t db = dst & 0xff;

    if (a == 0)
        return dst;
    if (a == 255)
        return src;
    dr = (sr * a + dr * inv) / 255;
    dg = (sg * a + dg * inv) / 255;
    db = (sb * a + db * inv) / 255;
    return 0xff000000u | (dr << 16) | (dg << 8) | db;
}

void xv6_icon_draw(uint32_t *fb, int fb_w, int fb_h, int x, int y,
                   int w, int h, const struct xv6_icon *icon)
{
    if (!fb || !icon || icon->width <= 0 || icon->height <= 0 ||
        w <= 0 || h <= 0)
        return;

    for (int dy = 0; dy < h; dy++) {
        int sy = dy * icon->height / h;

        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            int py = y + dy;
            int sx = dx * icon->width / w;
            uint32_t src;

            if (!wlcomp_draw_pixel_in_clip(px, py, fb_w, fb_h))
                continue;
            src = icon->pixels[sy * icon->width + sx];
            fb[py * fb_w + px] = blend_argb(fb[py * fb_w + px], src);
        }
    }
}
