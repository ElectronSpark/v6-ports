#define _GNU_SOURCE
#include <ctype.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void WebKitWebView;
typedef void (*webkit_web_view_load_uri_fn)(WebKitWebView *, const char *);

static int
has_scheme(const char *s)
{
    size_t i;

    if (!s || !isalpha((unsigned char)s[0]))
        return 0;
    for (i = 1; s[i]; i++) {
        if (s[i] == ':')
            return 1;
        if (s[i] == '/' || s[i] == '?' || s[i] == '#')
            return 0;
        if (!(isalnum((unsigned char)s[i]) || s[i] == '+' ||
              s[i] == '-' || s[i] == '.'))
            return 0;
    }
    return 0;
}

static int
looks_like_host(const char *s)
{
    int saw_dot = 0;
    size_t i;

    if (!s || !s[0])
        return 0;
    for (i = 0; s[i] && s[i] != '/' && s[i] != '?' && s[i] != '#'; i++) {
        if (s[i] == '.')
            saw_dot = 1;
        if (!(isalnum((unsigned char)s[i]) || s[i] == '.' ||
              s[i] == '-' || s[i] == ':'))
            return 0;
    }
    return saw_dot || strncmp(s, "localhost", 9) == 0;
}

static void
copy_prefixed(char *out, size_t out_size, const char *prefix, const char *value)
{
    size_t prefix_len = strlen(prefix);
    size_t value_len = strlen(value);

    if (out_size == 0)
        return;
    if (prefix_len > out_size - 1)
        prefix_len = out_size - 1;
    if (value_len > out_size - prefix_len - 1)
        value_len = out_size - prefix_len - 1;
    memcpy(out, prefix, prefix_len);
    memcpy(out + prefix_len, value, value_len);
    out[prefix_len + value_len] = '\0';
}

static const char *
normalize_uri(const char *uri, char *buf, size_t buf_size)
{
    char tmp[2048];
    size_t len = 0;

    if (!uri || buf_size == 0)
        return uri;
    while (*uri == ' ' || *uri == '\t' || *uri == '\n')
        uri++;
    while (uri[len] && uri[len] != ' ' && uri[len] != '\t' &&
           uri[len] != '\n' && len + 1 < sizeof(tmp)) {
        tmp[len] = uri[len];
        len++;
    }
    tmp[len] = '\0';
    if (!tmp[0])
        return uri;

    if (strncmp(tmp, "http//", 6) == 0) {
        copy_prefixed(buf, buf_size, "http://", tmp + 6);
        return buf;
    }
    if (strncmp(tmp, "https//", 7) == 0) {
        copy_prefixed(buf, buf_size, "https://", tmp + 7);
        return buf;
    }
    if (strncmp(tmp, "http:/", 6) == 0 && strncmp(tmp, "http://", 7) != 0) {
        copy_prefixed(buf, buf_size, "http://", tmp + 6);
        return buf;
    }
    if (strncmp(tmp, "https:/", 7) == 0 &&
        strncmp(tmp, "https://", 8) != 0) {
        copy_prefixed(buf, buf_size, "https://", tmp + 7);
        return buf;
    }
    if (has_scheme(tmp))
        return uri;
    if (tmp[0] == '/') {
        copy_prefixed(buf, buf_size, "file://", tmp);
        return buf;
    }
    if (looks_like_host(tmp)) {
        copy_prefixed(buf, buf_size, "https://", tmp);
        return buf;
    }
    return uri;
}

static int
is_youtube_uri(const char *uri)
{
    return uri && (strstr(uri, "youtube.com") ||
                   strstr(uri, "youtube-nocookie.com"));
}

static void
apply_youtube_media_env(const char *uri)
{
    const char *current_avc1;

    if (!is_youtube_uri(uri))
        return;

    current_avc1 = getenv("WEBKIT_GST_MAX_AVC1_RESOLUTION");
    if (!current_avc1 || current_avc1[0] == '\0' ||
        strcmp(current_avc1, "720P") == 0)
        setenv("WEBKIT_GST_MAX_AVC1_RESOLUTION", "360P", 1);
    if (!getenv("GST_PLUGIN_FEATURE_RANK") ||
        getenv("GST_PLUGIN_FEATURE_RANK")[0] == '\0')
        setenv("GST_PLUGIN_FEATURE_RANK",
               "vp9dec:0,avdec_vp9:0,avdec_av1:0", 1);
    if (getenv("XV6_WEBKIT_URI_LOG"))
        fprintf(stderr,
                "xv6-webkit-uri: youtube media env max_avc1=%s feature_rank=%s\n",
                getenv("WEBKIT_GST_MAX_AVC1_RESOLUTION"),
                getenv("GST_PLUGIN_FEATURE_RANK"));
}

void
webkit_web_view_load_uri(WebKitWebView *view, const char *uri)
{
    static webkit_web_view_load_uri_fn real_load_uri;
    char normalized[2048];
    const char *effective;

    if (!real_load_uri) {
        real_load_uri = (webkit_web_view_load_uri_fn)dlsym(
            RTLD_NEXT, "webkit_web_view_load_uri");
        if (!real_load_uri) {
            fprintf(stderr,
                    "xv6-webkit-uri: missing real webkit_web_view_load_uri\n");
            abort();
        }
    }

    effective = normalize_uri(uri, normalized, sizeof(normalized));
    if (effective != uri && getenv("XV6_WEBKIT_URI_LOG"))
        fprintf(stderr, "xv6-webkit-uri: normalized '%s' -> '%s'\n",
                uri ? uri : "(null)", effective ? effective : "(null)");
    apply_youtube_media_env(effective);
    real_load_uri(view, effective);
}
