#include <stddef.h>
#include <stdint.h>

void *
memset(void *dst, int value, size_t len)
{
    unsigned char *p = dst;

    for (size_t i = 0; i < len; i++)
        p[i] = (unsigned char)value;
    return dst;
}

void *
memcpy(void *dst, const void *src, size_t len)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    for (size_t i = 0; i < len; i++)
        d[i] = s[i];
    return dst;
}

void *
memmove(void *dst, const void *src, size_t len)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    if (d == s || len == 0)
        return dst;
    if (d < s) {
        for (size_t i = 0; i < len; i++)
            d[i] = s[i];
    } else {
        for (size_t i = len; i > 0; i--)
            d[i - 1] = s[i - 1];
    }
    return dst;
}

void *
__memset_chk(void *dst, int value, size_t len, size_t dstlen)
{
    if (len > dstlen)
        __builtin_trap();
    return memset(dst, value, len);
}

void *
__memcpy_chk(void *dst, const void *src, size_t len, size_t dstlen)
{
    if (len > dstlen)
        __builtin_trap();
    return memcpy(dst, src, len);
}

void *
__memmove_chk(void *dst, const void *src, size_t len, size_t dstlen)
{
    if (len > dstlen)
        __builtin_trap();
    return memmove(dst, src, len);
}

