#include <dlfcn.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef uintptr_t xv6_mem_word_t __attribute__((__may_alias__));

#define XV6_X86_64_UREG_RDI 8
#define XV6_X86_64_UREG_RAX 13
#define XV6_X86_64_UREG_RIP 16
#define XV6_X86_64_UREG_CR2 22

struct xv6_sig_mcontext {
    long long gregs[23];
    void *fpregs;
    uint64_t reserved[8];
};

struct xv6_sig_ucontext {
    unsigned long uc_flags;
    struct xv6_sig_ucontext *uc_link;
    stack_t uc_stack;
    struct xv6_sig_mcontext uc_mcontext;
};

static struct sigaction xv6_old_segv_action;
static int xv6_old_segv_action_valid;
static int xv6_webkit_skia_recovery_installed;

static int
xv6_env_disabled(const char *name)
{
    const char *value = getenv(name);

    return value && (!value[0] || strcmp(value, "0") == 0 ||
                     strcmp(value, "no") == 0 ||
                     strcmp(value, "false") == 0 ||
                     strcmp(value, "off") == 0);
}

static void
xv6_chain_segv(int signo, siginfo_t *info, void *context)
{
    if (xv6_old_segv_action_valid) {
        if (xv6_old_segv_action.sa_flags & SA_SIGINFO) {
            if (xv6_old_segv_action.sa_sigaction) {
                xv6_old_segv_action.sa_sigaction(signo, info, context);
                return;
            }
        } else if (xv6_old_segv_action.sa_handler &&
                   xv6_old_segv_action.sa_handler != SIG_DFL &&
                   xv6_old_segv_action.sa_handler != SIG_IGN) {
            xv6_old_segv_action.sa_handler(signo);
            return;
        } else if (xv6_old_segv_action.sa_handler == SIG_IGN) {
            return;
        }
    }
    signal(signo, SIG_DFL);
    raise(signo);
}

static void
xv6_webkit_skia_segv_handler(int signo, siginfo_t *info, void *context)
{
    static const unsigned char null_member_helper[] = {
        0x48, 0x8b, 0x47, 0x28, 0x48, 0x85, 0xc0, 0x74, 0x01, 0xc3
    };
    struct xv6_sig_ucontext *uc = (struct xv6_sig_ucontext *)context;
    unsigned char *rip;

    if (signo == SIGSEGV && info && context &&
        info->si_addr == (void *)0x28 &&
        uc->uc_mcontext.gregs[XV6_X86_64_UREG_CR2] == 0x28 &&
        uc->uc_mcontext.gregs[XV6_X86_64_UREG_RDI] == 0) {
        rip = (unsigned char *)(uintptr_t)
            uc->uc_mcontext.gregs[XV6_X86_64_UREG_RIP];
        if (memcmp(rip, null_member_helper,
                   sizeof(null_member_helper)) == 0) {
            static int logs;

            uc->uc_mcontext.gregs[XV6_X86_64_UREG_RAX] = 0;
            uc->uc_mcontext.gregs[XV6_X86_64_UREG_RIP] += 9;
            if (logs < 8) {
                static const char msg[] =
                    "xv6-webkit-skia: recovered null member lookup\n";
                ssize_t ignored = write(2, msg, sizeof(msg) - 1);
                (void)ignored;
                logs++;
            }
            return;
        }
    }
    xv6_chain_segv(signo, info, context);
}

static void
xv6_webkit_skia_signal_recovery_install_locked(int force)
{
    struct sigaction sa;
    struct sigaction cur;

    if (xv6_env_disabled("XV6_WEBKIT_SKIA_NULL_MEMBER_RECOVER"))
        return;
    if (xv6_webkit_skia_recovery_installed && !force)
        return;
    if (force &&
        sigaction(SIGSEGV, NULL, &cur) == 0 &&
        (cur.sa_flags & SA_SIGINFO) &&
        cur.sa_sigaction == xv6_webkit_skia_segv_handler)
        return;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = xv6_webkit_skia_segv_handler;
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGSEGV, &sa, &xv6_old_segv_action) == 0) {
        xv6_old_segv_action_valid = 1;
        xv6_webkit_skia_recovery_installed = 1;
        if (!xv6_env_disabled("XV6_WEBKIT_SKIA_NULL_MEMBER_RECOVER_LOG")) {
            static const char msg[] =
                "xv6-webkit-skia: SIGSEGV recovery installed\n";
            ssize_t ignored = write(2, msg, sizeof(msg) - 1);
            (void)ignored;
        }
    }
}

void
xv6_webkit_skia_signal_recovery_force_install(void)
{
    xv6_webkit_skia_signal_recovery_install_locked(1);
}

__attribute__((constructor))
static void
xv6_webkit_skia_signal_recovery_init(void)
{
    xv6_webkit_skia_signal_recovery_install_locked(0);
}

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
typedef int (*xv6_g_source_func)(void *data);
typedef unsigned int (*xv6_g_timeout_add_fn)(unsigned int interval,
                                             xv6_g_source_func function,
                                             void *data);
typedef void (*xv6_webkit_eval_js_fn)(void *web_view, const char *script,
                                      long length, const char *world_name,
                                      const char *source_uri,
                                      void *cancellable, void *callback,
                                      void *user_data);
typedef void *(*xv6_webkit_eval_finish_fn)(void *web_view, void *result,
                                           void **error);
typedef char *(*xv6_jsc_value_to_string_fn)(void *value);
typedef void (*xv6_g_free_fn)(void *ptr);
typedef void (*xv6_g_object_unref_fn)(void *object);

static void *xv6_youtube_probe_view;
static int xv6_youtube_probe_installed;
static time_t xv6_youtube_probe_start;
static int xv6_youtube_probe_seconds;

static int
xv6_webkit_youtube_url(const char *uri)
{
    return uri && (strstr(uri, "youtube.com") ||
                   strstr(uri, "youtube-nocookie.com") ||
                   strstr(uri, "youtu.be"));
}

static int
xv6_webkit_youtube_probe_seconds(void)
{
    const char *value;
    int seconds;

    if (xv6_youtube_probe_seconds > 0)
        return xv6_youtube_probe_seconds;
    value = getenv("XV6_WEBKIT_YOUTUBE_PROBE_SECONDS");
    seconds = value ? atoi(value) : 180;
    if (seconds <= 0)
        seconds = 180;
    if (seconds > 3600)
        seconds = 3600;
    xv6_youtube_probe_seconds = seconds;
    return seconds;
}

static void
xv6_write_media_probe_line(const char *line)
{
    FILE *fp;

    if (!line)
        return;
    fp = fopen("/tmp/webkit-media-probe", "w");
    if (!fp)
        return;
    fprintf(fp, "%s\n", line);
    fclose(fp);
}

static void
xv6_youtube_probe_result_cb(void *source_object, void *result, void *user_data)
{
    static xv6_webkit_eval_finish_fn eval_finish;
    static xv6_jsc_value_to_string_fn value_to_string;
    static xv6_g_free_fn g_free_fn;
    static xv6_g_object_unref_fn g_object_unref_fn;
    void *value;
    char *text;

    (void)user_data;
    if (!eval_finish)
        eval_finish = (xv6_webkit_eval_finish_fn)dlsym(
            RTLD_NEXT, "webkit_web_view_evaluate_javascript_finish");
    if (!value_to_string)
        value_to_string = (xv6_jsc_value_to_string_fn)dlsym(
            RTLD_NEXT, "jsc_value_to_string");
    if (!g_free_fn)
        g_free_fn = (xv6_g_free_fn)dlsym(RTLD_NEXT, "g_free");
    if (!g_object_unref_fn)
        g_object_unref_fn = (xv6_g_object_unref_fn)dlsym(RTLD_NEXT,
                                                         "g_object_unref");
    if (!eval_finish || !value_to_string)
        return;

    value = eval_finish(source_object, result, NULL);
    if (!value)
        return;
    text = value_to_string(value);
    if (text && text[0])
        xv6_write_media_probe_line(text);
    if (text && g_free_fn)
        g_free_fn(text);
    if (g_object_unref_fn)
        g_object_unref_fn(value);
}

static int
xv6_youtube_probe_tick(void *data)
{
    static xv6_webkit_eval_js_fn eval_js;
    static const char script[] =
        "(function(){"
        "var T={vp9:'video/webm; codecs=\"vp9\"',"
        "vp9opus:'video/webm; codecs=\"vp9, opus\"',"
        "av1mp4:'video/mp4; codecs=\"av01.0.05M.08\"',"
        "av1webm:'video/webm; codecs=\"av01.0.05M.08\"',"
        "opus:'audio/webm; codecs=\"opus\"',"
        "avc:'video/mp4; codecs=\"avc1.42E01E, mp4a.40.2\"'};"
        "function mse(t){try{return !!(window.MediaSource&&"
        "MediaSource.isTypeSupported&&MediaSource.isTypeSupported(t));}"
        "catch(e){return 'throw-'+e.name;}}"
        "function cpt(t){try{var e=document.createElement("
        "t.indexOf('audio/')===0?'audio':'video');return e.canPlayType(t)||'no';}"
        "catch(e){return 'throw-'+e.name;}}"
        "function ev(s){if(window.__xv6yt&&window.__xv6yt.e.indexOf(s)<0)"
        "window.__xv6yt.e.push(s);}"
        "if(!window.__xv6yt)window.__xv6yt={e:[],max:0};"
        "var v=document.querySelector('video');"
        "if(v&&!v.__xv6yt){['loadstart','loadedmetadata','canplay','playing',"
        "'timeupdate','waiting','stalled','suspend','error'].forEach(function(n){"
        "v.addEventListener(n,function(){ev(n);});});v.__xv6yt=1;}"
        "if(v){try{v.muted=true;v.playsInline=true;"
        "if(v.paused&&v.play)v.play().catch(function(){});}catch(e){}}"
        "var q=0;if(v){try{var p=v.getVideoPlaybackQuality&&"
        "v.getVideoPlaybackQuality();q=p?p.totalVideoFrames:0;}catch(e){}"
        "if(!q&&typeof v.webkitDecodedFrameCount==='number')q=v.webkitDecodedFrameCount;"
        "if(v.currentTime>window.__xv6yt.max)window.__xv6yt.max=v.currentTime;}"
        "var r=v?v.getBoundingClientRect():{width:0,height:0,left:0,top:0};"
        "var b=0;if(v&&v.buffered&&v.buffered.length){try{b=v.buffered.end(v.buffered.length-1);}catch(e){}}"
        "return 'XV6-YTMEDIA mse_vp9='+mse(T.vp9)+' mse_vp9opus='+mse(T.vp9opus)+"
        "' mse_av1mp4='+mse(T.av1mp4)+' mse_av1webm='+mse(T.av1webm)+"
        "' mse_opus='+mse(T.opus)+' mse_avc='+mse(T.avc)+"
        "' can_vp9='+cpt(T.vp9)+' can_av1mp4='+cpt(T.av1mp4)+"
        "' can_av1webm='+cpt(T.av1webm)+' can_opus='+cpt(T.opus)+"
        "' can_avc='+cpt(T.avc)+' video='+(v?1:0)+"
        "' t='+(v?v.currentTime.toFixed(2):'0.00')+"
        "' max='+(window.__xv6yt.max||0).toFixed(2)+"
        "' buf='+b.toFixed(2)+' vw='+(v?v.videoWidth:0)+"
        "' vh='+(v?v.videoHeight:0)+' rect='+Math.round(r.left)+','+Math.round(r.top)+"
        "'x'+Math.round(r.width)+'x'+Math.round(r.height)+"
        "' frames='+q+' rs='+(v?v.readyState:-1)+' ns='+(v?v.networkState:-1)+"
        "' paused='+(v&&v.paused?1:0)+' err='+(v&&v.error?v.error.code:0)+"
        "' events='+window.__xv6yt.e.join('|');"
        "})()";
    time_t now;

    (void)data;
    if (!xv6_youtube_probe_view)
        return 0;
    now = time(NULL);
    if (xv6_youtube_probe_start > 0 &&
        now - xv6_youtube_probe_start >
            xv6_webkit_youtube_probe_seconds())
        return 0;
    if (!eval_js)
        eval_js = (xv6_webkit_eval_js_fn)dlsym(
            RTLD_NEXT, "webkit_web_view_evaluate_javascript");
    if (!eval_js) {
        xv6_write_media_probe_line("XV6-YTMEDIA eval_javascript=missing");
        return 0;
    }
    eval_js(xv6_youtube_probe_view, script, -1, NULL, NULL, NULL,
            (void *)xv6_youtube_probe_result_cb, NULL);
    return 1;
}

static void
xv6_webkit_youtube_probe_start(void *web_view, const char *uri)
{
    static xv6_g_timeout_add_fn g_timeout_add_fn;

    if (!web_view || !xv6_webkit_youtube_url(uri) ||
        xv6_env_disabled("XV6_WEBKIT_YOUTUBE_PROBE"))
        return;
    xv6_youtube_probe_view = web_view;
    xv6_youtube_probe_start = time(NULL);
    if (xv6_youtube_probe_installed)
        return;
    if (!g_timeout_add_fn)
        g_timeout_add_fn = (xv6_g_timeout_add_fn)dlsym(RTLD_NEXT,
                                                       "g_timeout_add");
    if (!g_timeout_add_fn) {
        xv6_write_media_probe_line("XV6-YTMEDIA g_timeout_add=missing");
        return;
    }
    xv6_youtube_probe_installed = 1;
    g_timeout_add_fn(1000, xv6_youtube_probe_tick, NULL);
    xv6_write_media_probe_line("XV6-YTMEDIA probe=armed");
}

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

    xv6_webkit_skia_signal_recovery_force_install();
    load_uri = xv6_normalize_webkit_uri(uri, normalized, sizeof(normalized), 1);
    if (xv6_webkit_uri_log_enabled())
        fprintf(stderr, "xv6-webkit-uri: load_uri '%s'\n",
                load_uri ? load_uri : "(null)");
    if (load_uri && uri && load_uri != uri && xv6_webkit_uri_log_enabled())
        fprintf(stderr, "xv6-webkit-uri: normalized '%s' -> '%s'\n",
                uri, load_uri);
    real_load_uri(web_view, load_uri);
    xv6_webkit_youtube_probe_start(web_view, load_uri);
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

    xv6_webkit_skia_signal_recovery_force_install();
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

    xv6_webkit_skia_signal_recovery_force_install();
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

    xv6_webkit_skia_signal_recovery_force_install();
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
    if (request && real_request_get_uri)
        xv6_webkit_youtube_probe_start(web_view, real_request_get_uri(request));
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

    xv6_webkit_skia_signal_recovery_force_install();
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
