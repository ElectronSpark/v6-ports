#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

typedef uintptr_t xv6_mem_word_t __attribute__((__may_alias__));

void *
memset(void *dst, int value, size_t len)
{
    unsigned char *p = dst;
    uintptr_t word;
    size_t word_size = sizeof(xv6_mem_word_t);

    if (len == 0)
        return dst;

    word = (unsigned char)value;
    word |= word << 8;
    word |= word << 16;
#if UINTPTR_MAX > 0xffffffffU
    word |= word << 32;
#endif

    while (len > 0 && ((uintptr_t)p & (word_size - 1)) != 0) {
        *p++ = (unsigned char)value;
        len--;
    }

    while (len >= word_size) {
        *(xv6_mem_word_t *)p = word;
        p += word_size;
        len -= word_size;
    }

    while (len-- > 0)
        *p++ = (unsigned char)value;
    return dst;
}

void *
memcpy(void *dst, const void *src, size_t len)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    size_t word_size = sizeof(xv6_mem_word_t);

    if (len == 0 || d == s)
        return dst;

    if ((((uintptr_t)d | (uintptr_t)s) & (word_size - 1)) == 0) {
        while (len >= word_size) {
            *(xv6_mem_word_t *)d = *(const xv6_mem_word_t *)s;
            d += word_size;
            s += word_size;
            len -= word_size;
        }
    }

    while (len-- > 0)
        *d++ = *s++;
    return dst;
}

void *
memmove(void *dst, const void *src, size_t len)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    size_t word_size = sizeof(xv6_mem_word_t);

    if (d == s || len == 0)
        return dst;
    if (d < s) {
        if ((((uintptr_t)d | (uintptr_t)s) & (word_size - 1)) == 0) {
            while (len >= word_size) {
                *(xv6_mem_word_t *)d = *(const xv6_mem_word_t *)s;
                d += word_size;
                s += word_size;
                len -= word_size;
            }
        }
        while (len-- > 0)
            *d++ = *s++;
    } else {
        d += len;
        s += len;
        if ((((uintptr_t)d | (uintptr_t)s) & (word_size - 1)) == 0) {
            while (len >= word_size) {
                d -= word_size;
                s -= word_size;
                len -= word_size;
                *(xv6_mem_word_t *)d = *(const xv6_mem_word_t *)s;
            }
        }
        while (len-- > 0)
            *--d = *--s;
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

static int
xv6_uri_has_scheme(const char *s)
{
    size_t i;

    if (!s || !((s[0] >= 'A' && s[0] <= 'Z') ||
                (s[0] >= 'a' && s[0] <= 'z')))
        return 0;
    for (i = 1; s[i]; i++) {
        if (s[i] == ':')
            return 1;
        if (s[i] == '/' || s[i] == '?' || s[i] == '#')
            return 0;
        if (!((s[i] >= 'A' && s[i] <= 'Z') ||
              (s[i] >= 'a' && s[i] <= 'z') ||
              (s[i] >= '0' && s[i] <= '9') ||
              s[i] == '+' || s[i] == '-' || s[i] == '.'))
            return 0;
    }
    return 0;
}

static int
xv6_uri_looks_like_host(const char *s)
{
    int saw_dot = 0;
    size_t i;

    if (!s || !s[0])
        return 0;
    for (i = 0; s[i] && s[i] != '/' && s[i] != '?' && s[i] != '#'; i++) {
        if (s[i] == '.')
            saw_dot = 1;
        if (s[i] == ':')
            continue;
        if (!(s[i] == '.' || s[i] == '-' ||
              (s[i] >= '0' && s[i] <= '9') ||
              (s[i] >= 'A' && s[i] <= 'Z') ||
              (s[i] >= 'a' && s[i] <= 'z')))
            return 0;
    }
    return saw_dot || strncmp(s, "localhost", 9) == 0;
}

static size_t
xv6_uri_copy(char *out, size_t out_size, const char *s)
{
    size_t n = 0;

    if (!out_size)
        return 0;
    while (s && *s && n + 1 < out_size)
        out[n++] = *s++;
    out[n] = '\0';
    return n;
}

static void
xv6_uri_copy_escaped(char *out, size_t out_size, const char *prefix,
                     const char *value)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n;

    n = xv6_uri_copy(out, out_size, prefix);
    for (const unsigned char *p = (const unsigned char *)value;
         p && *p && n + 1 < out_size; p++) {
        if (*p <= 0x20 || *p == '"' || *p == '<' || *p == '>' || *p == '`') {
            if (n + 3 >= out_size)
                break;
            out[n++] = '%';
            out[n++] = hex[*p >> 4];
            out[n++] = hex[*p & 0x0f];
        } else {
            out[n++] = (char)*p;
        }
    }
    out[n] = '\0';
}

static const char *
xv6_normalize_webkit_uri(const char *uri, char *buf, size_t buf_size,
                         int allow_search)
{
    const char *start;
    const char *end;
    char tmp[1024];
    size_t len;

    if (!uri || !buf_size)
        return uri;

    start = uri;
    while (*start == ' ' || *start == '\t' || *start == '\n' ||
           *start == '\r')
        start++;
    end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' ||
                           end[-1] == '\n' || end[-1] == '\r'))
        end--;

    len = (size_t)(end - start);
    if (len >= sizeof(tmp))
        return uri;
    memcpy(tmp, start, len);
    tmp[len] = '\0';

    if (!tmp[0])
        return uri;

    if (strncmp(tmp, "http//", 6) == 0) {
        xv6_uri_copy_escaped(buf, buf_size, "http://", tmp + 6);
        return buf;
    }
    if (strncmp(tmp, "https//", 7) == 0) {
        xv6_uri_copy_escaped(buf, buf_size, "https://", tmp + 7);
        return buf;
    }
    if (strncmp(tmp, "http:/", 6) == 0 && strncmp(tmp, "http://", 7) != 0) {
        xv6_uri_copy_escaped(buf, buf_size, "http://", tmp + 6);
        return buf;
    }
    if (strncmp(tmp, "https:/", 7) == 0 && strncmp(tmp, "https://", 8) != 0) {
        xv6_uri_copy_escaped(buf, buf_size, "https://", tmp + 7);
        return buf;
    }
    if (xv6_uri_has_scheme(tmp)) {
        xv6_uri_copy_escaped(buf, buf_size, "", tmp);
        return strcmp(buf, uri) == 0 ? uri : buf;
    }
    if (tmp[0] == '/') {
        xv6_uri_copy_escaped(buf, buf_size, "file://", tmp);
        return buf;
    }
    if (xv6_uri_looks_like_host(tmp)) {
        xv6_uri_copy_escaped(buf, buf_size, "https://", tmp);
        return buf;
    }

    if (allow_search) {
        xv6_uri_copy_escaped(buf, buf_size,
                             "https://www.google.com/search?q=", tmp);
        return buf;
    }
    return uri;
}

static int
xv6_webkit_uri_log_enabled(void)
{
    static int enabled = -1;
    const char *value;

    if (enabled >= 0)
        return enabled;
    value = getenv("XV6_WEBKIT_URI_LOG");
    enabled = value && value[0] && strcmp(value, "0") != 0;
    return enabled;
}

typedef void (*xv6_webkit_load_uri_fn)(void *web_view, const char *uri);

void
webkit_web_view_load_uri(void *web_view, const char *uri)
{
    static xv6_webkit_load_uri_fn real_load_uri;
    char normalized[1200];
    const char *load_uri;

    if (!real_load_uri)
        real_load_uri = (xv6_webkit_load_uri_fn)dlsym(RTLD_NEXT,
                                                      "webkit_web_view_load_uri");
    if (!real_load_uri)
        return;

    load_uri = xv6_normalize_webkit_uri(uri, normalized, sizeof(normalized), 1);
    if (xv6_webkit_uri_log_enabled())
        fprintf(stderr, "xv6-webkit-uri: load_uri '%s'\n",
                load_uri ? load_uri : "(null)");
    if (load_uri && uri && load_uri != uri && xv6_webkit_uri_log_enabled())
        fprintf(stderr, "xv6-webkit-uri: normalized '%s' -> '%s'\n",
                uri, load_uri);
    real_load_uri(web_view, load_uri);
}

typedef void *(*xv6_webkit_uri_request_new_fn)(const char *uri);

void *
webkit_uri_request_new(const char *uri)
{
    static xv6_webkit_uri_request_new_fn real_request_new;
    char normalized[1200];
    const char *request_uri;

    if (!real_request_new)
        real_request_new = (xv6_webkit_uri_request_new_fn)dlsym(
            RTLD_NEXT, "webkit_uri_request_new");
    if (!real_request_new)
        return NULL;

    request_uri = xv6_normalize_webkit_uri(uri, normalized,
                                           sizeof(normalized), 1);
    if (request_uri && uri && request_uri != uri &&
        xv6_webkit_uri_log_enabled())
        fprintf(stderr, "xv6-webkit-uri: normalized request '%s' -> '%s'\n",
                uri, request_uri);
    return real_request_new(request_uri);
}

typedef void *(*xv6_g_uri_parse_fn)(const char *uri, unsigned int flags,
                                    void **error);

void *
g_uri_parse(const char *uri, unsigned int flags, void **error)
{
    static xv6_g_uri_parse_fn real_g_uri_parse;
    char normalized[1200];
    const char *parse_uri;

    if (!real_g_uri_parse)
        real_g_uri_parse = (xv6_g_uri_parse_fn)dlsym(RTLD_NEXT,
                                                     "g_uri_parse");
    if (!real_g_uri_parse)
        return NULL;

    parse_uri = xv6_normalize_webkit_uri(uri, normalized,
                                         sizeof(normalized), 0);
    if (parse_uri && uri && parse_uri != uri && xv6_webkit_uri_log_enabled())
        fprintf(stderr, "xv6-webkit-uri: normalized parse '%s' -> '%s'\n",
                uri, parse_uri);
    return real_g_uri_parse(parse_uri, flags, error);
}

typedef void (*xv6_webkit_load_request_fn)(void *web_view, void *request);
typedef const char *(*xv6_webkit_request_get_uri_fn)(void *request);
typedef void (*xv6_webkit_request_set_uri_fn)(void *request, const char *uri);

void
webkit_web_view_load_request(void *web_view, void *request)
{
    static xv6_webkit_load_request_fn real_load_request;
    static xv6_webkit_request_get_uri_fn real_request_get_uri;
    static xv6_webkit_request_set_uri_fn real_request_set_uri;
    char normalized[1200];

    if (!real_load_request)
        real_load_request = (xv6_webkit_load_request_fn)dlsym(
            RTLD_NEXT, "webkit_web_view_load_request");
    if (!real_request_get_uri)
        real_request_get_uri = (xv6_webkit_request_get_uri_fn)dlsym(
            RTLD_NEXT, "webkit_uri_request_get_uri");
    if (!real_request_set_uri)
        real_request_set_uri = (xv6_webkit_request_set_uri_fn)dlsym(
            RTLD_NEXT, "webkit_uri_request_set_uri");
    if (!real_load_request)
        return;

    if (request && real_request_get_uri && real_request_set_uri) {
        const char *uri = real_request_get_uri(request);
        const char *request_uri = xv6_normalize_webkit_uri(
            uri, normalized, sizeof(normalized), 1);

        if (xv6_webkit_uri_log_enabled())
            fprintf(stderr, "xv6-webkit-uri: load_request '%s'\n",
                    request_uri ? request_uri : "(null)");
        if (request_uri && uri && request_uri != uri) {
            if (xv6_webkit_uri_log_enabled())
                fprintf(stderr,
                        "xv6-webkit-uri: normalized load request '%s' -> '%s'\n",
                        uri, request_uri);
            real_request_set_uri(request, request_uri);
        }
    }

    real_load_request(web_view, request);
}

typedef const char *(*xv6_gtk_entry_get_text_fn)(void *entry);

const char *
gtk_entry_get_text(void *entry)
{
    static xv6_gtk_entry_get_text_fn real_entry_get_text;
    static __thread char normalized[1200];
    const char *text;
    const char *entry_text;

    if (!real_entry_get_text)
        real_entry_get_text = (xv6_gtk_entry_get_text_fn)dlsym(
            RTLD_NEXT, "gtk_entry_get_text");
    if (!real_entry_get_text)
        return "";

    text = real_entry_get_text(entry);
    entry_text = xv6_normalize_webkit_uri(text, normalized,
                                          sizeof(normalized), 1);
    if (entry_text && text && entry_text != text) {
        if (xv6_webkit_uri_log_enabled())
            fprintf(stderr, "xv6-webkit-uri: normalized entry '%s' -> '%s'\n",
                    text, entry_text);
        return entry_text;
    }
    return text;
}
