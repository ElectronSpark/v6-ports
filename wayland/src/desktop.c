/*
 * desktop.c — xv6 Wayland Session Manager
 *
 * Launches the wlcomp Wayland compositor, then starts Wayland clients
 * (e.g. NetSurf browser).  Monitors child processes and performs clean
 * shutdown on SIGTERM / SIGINT.
 *
 * Started automatically by init via /etc/daemons.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>

#define WAYLAND_SOCKET_PATH  "/tmp/wayland-0.lock" /* lockfile (real VFS file) */
#define SOCKET_WAIT_TRIES    200      /* 200 × 20 ms = 4 s */
#define SOCKET_WAIT_US       20000
#define WEBKIT_NET_WAIT_US   35000000 /* DHCP fallback/network daemons need ~30s */
#define WEBKIT_DEFAULT_URL   "https://www.google.com/search?q=xv6&gbv=1"
#define WEBKIT_URL_MAX       768
#define XV6_DRM_RENDER_NODE  "/dev/dri/renderD128"
#define DRM_IOCTL_VIRTGPU_GETPARAM 0xc0106443UL
#define VIRTGPU_PARAM_3D_FEATURES  1

static const char *webkit_feature_flags =
    "--features=+OffscreenCanvas,+OffscreenCanvasInWorkers,+requestIdleCallback";
static const char *webkit_feature_flags_no_idle =
    "--features=+OffscreenCanvas,+OffscreenCanvasInWorkers,-requestIdleCallback";
static const char *webkit_youtube_compat_user_agent =
    "--user-agent=Mozilla/5.0 (X11; xv6 x86_64) AppleWebKit/605.1.15 "
    "(KHTML, like Gecko) Version/17.0 Safari/605.1.15";

struct drm_virtgpu_getparam_compat {
    uint64_t param;
    uint64_t value;
};

static volatile sig_atomic_t g_running = 1;
static pid_t wlcomp_pid;
static pid_t client_pid;
static pid_t httpd_pid;

static int webkit_accel_enabled_by_cmdline(void);
static int webkit_gpu_smoke_enabled_by_cmdline(void);
static int webkit_webgl_smoke_enabled_by_cmdline(void);
static int webkit_api_smoke_enabled_by_cmdline(void);
static int webkit_http_smoke_enabled_by_cmdline(void);
static int webkit_coop_smoke_enabled_by_cmdline(void);
static int webkit_js_smoke_enabled_by_cmdline(void);
static int webkit_youtube_boot_smoke_enabled_by_cmdline(void);
static int webkit_youtube_waterfall_smoke_enabled_by_cmdline(void);
static int webkit_youtube_compat_disabled_by_cmdline(void);
static int webkit_request_idle_disabled_by_cmdline(void);
static int webkit_feature_gate_smoke_enabled_by_cmdline(void);
static int webkit_idle_browse_smoke_enabled_by_cmdline(void);
static int webkit_compat_gate_smoke_enabled_by_cmdline(void);
static int webkit_js_disabled_by_cmdline(void);
static int webkit_reopen_count_from_cmdline(void);
static int webkit_timeout_ms_from_cmdline(int fallback);
static int desktop_disabled_by_cmdline(void);
static int read_cmdline(char *buf, size_t buf_size);

static int xv6_virgl_available(void)
{
    uint64_t value = 0;
    struct drm_virtgpu_getparam_compat req = {
        .param = VIRTGPU_PARAM_3D_FEATURES,
        .value = (uint64_t)(uintptr_t)&value,
    };
    int fd = open(XV6_DRM_RENDER_NODE, O_RDWR | O_CLOEXEC);
    int ok;

    if (fd < 0)
        return 0;
    ok = ioctl(fd, DRM_IOCTL_VIRTGPU_GETPARAM, &req) == 0 && value != 0;
    close(fd);
    return ok;
}

static void disable_child_coredumps(void)
{
    struct rlimit lim = {0, 0};
    (void)setrlimit(RLIMIT_CORE, &lim);
}

static long long monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void sighandler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* Wait for wlcomp to create the Wayland socket.  Returns 0 on success. */
static int wait_for_socket(void)
{
    struct stat st;
    for (int i = 0; i < SOCKET_WAIT_TRIES; i++) {
        if (stat(WAYLAND_SOCKET_PATH, &st) == 0)
            return 0;
        usleep(SOCKET_WAIT_US);
    }
    return -1;
}

static int url_needs_network_wait(const char *url)
{
    (void)url;
    return 0;
}

static int webkit_youtube_compat_url(const char *url)
{
    if (!url || webkit_youtube_compat_disabled_by_cmdline())
        return 0;
    return strstr(url, "youtube.com") != NULL ||
           strstr(url, "youtube-nocookie.com") != NULL ||
           strstr(url, "youtu.be") != NULL;
}

static pid_t launch_wlcomp(void)
{
    char cmdline_buf[512] = "";
    char cmdline_env[sizeof("XV6_KERNEL_CMDLINE=") + sizeof(cmdline_buf)];
    int have_cmdline = read_cmdline(cmdline_buf, sizeof(cmdline_buf)) == 0;
    pid_t pid = fork();
    if (pid == 0) {
        char *argv[] = { "wlcomp", NULL };
        char *envp_base[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XV6_GUI_SESSION=1",
            "XV6_WLCOMP_FB_DIRECT=1",
            "XV6_WLCOMP_FB_BO=1",
            NULL
        };
        char *envp_cmdline[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XV6_GUI_SESSION=1",
            "XV6_WLCOMP_FB_DIRECT=1",
            "XV6_WLCOMP_FB_BO=1",
            cmdline_env,
            NULL
        };
        if (have_cmdline)
            snprintf(cmdline_env, sizeof(cmdline_env), "XV6_KERNEL_CMDLINE=%s",
                     cmdline_buf);
        execve("/bin/wlcomp", argv, have_cmdline ? envp_cmdline : envp_base);
        _exit(127);
    }
    return pid;
}

static void run_http_smoke_server(void)
{
    static const char plain_body[] =
        "<!doctype html><title>xv6 plain HTTP smoke</title>"
        "<h1>xv6 plain HTTP smoke</h1>\n";
    static const char js_body[] =
        "<!doctype html><meta charset=utf-8>"
        "<title>xv6-js-smoke:boot</title>"
        "<h1 id=out>boot</h1><xv6-smoke></xv6-smoke>"
        "<script>"
        "window.__xv6Smoke=[];"
        "function mark(x){if(__xv6Smoke.indexOf(x)<0)__xv6Smoke.push(x);"
        "document.getElementById('out').textContent=__xv6Smoke.join(',');"
        "document.title='xv6-js-smoke:'+__xv6Smoke.join(',');"
        "console.log('XV6-JS-SMOKE '+__xv6Smoke.join(','));}"
        "function done(){var need=['external','ce','promise','microtask','timeout','raf','idle','fetch','fetch-stream','xhr','domcontent','load'];"
        "if(need.every(function(x){return __xv6Smoke.indexOf(x)>=0;})){document.title='xv6-js-smoke:PASS:'+__xv6Smoke.join(',');console.log('XV6-JS-SMOKE PASS');}}"
        "function hit(x){mark(x);done();}"
        "document.addEventListener('DOMContentLoaded',function(){hit('domcontent');});"
        "window.addEventListener('load',function(){hit('load');});"
        "customElements.define('xv6-smoke',class extends HTMLElement{connectedCallback(){hit('ce');}});"
        "Promise.resolve().then(function(){hit('promise');});"
        "queueMicrotask(function(){hit('microtask');});"
        "setTimeout(function(){hit('timeout');},20);"
        "requestAnimationFrame(function(){hit('raf');});"
        "requestIdleCallback(function(){hit('idle');},{timeout:1000});"
        "fetch('/json').then(function(r){return r.json();}).then(function(j){if(j.ok)hit('fetch');}).catch(function(e){console.error('XV6-JS-SMOKE fetch '+e);});"
        "fetch('/stream').then(function(r){if(!r.body||typeof r.body.getReader!=='function')throw new Error('missing body reader');var rd=r.body.getReader();var total=0;function pump(){return rd.read().then(function(x){if(x.done){if(total>0)hit('fetch-stream');else throw new Error('empty stream');return;}total+=x.value?x.value.byteLength:0;return pump();});}return pump();}).catch(function(e){console.error('XV6-JS-SMOKE fetch-stream '+e);});"
        "var x=new XMLHttpRequest();x.onload=function(){if(x.responseText==='ok')hit('xhr');};x.onerror=function(){console.error('XV6-JS-SMOKE xhr error');};x.open('GET','/xhr');x.send();"
        "</script><script src=/after.js></script>";
    static const char after_js[] = "hit('external');\n";
    static const char ytboot_body[] =
        "<!doctype html><meta charset=utf-8>"
        "<title>xv6-ytboot:boot</title><h1 id=out>boot</h1>"
        "<script>"
        "window.__xv6Boot=[];"
        "function mark(x){if(__xv6Boot.indexOf(x)<0)__xv6Boot.push(x);"
        "document.getElementById('out').textContent=__xv6Boot.join(',');"
        "document.title='xv6-ytboot:'+__xv6Boot.join(',');"
        "console.log('XV6-YTBOOT '+__xv6Boot.join(','));done();}"
        "function done(){var need=['small','large','postlarge','browse','domcontent','load'];"
        "if(need.every(function(x){return __xv6Boot.indexOf(x)>=0;})){document.title='xv6-ytboot:PASS:'+__xv6Boot.join(',');console.log('XV6-YTBOOT PASS');}}"
        "document.addEventListener('DOMContentLoaded',function(){mark('domcontent');});"
        "window.addEventListener('load',function(){mark('load');});"
        "</script><script src=/small.js></script><script src=/large.js></script>"
        "<script>mark('postlarge');</script>";
    static const char small_js[] = "mark('small');\n";
    static const char large_js_prefix[] =
        "window.__xv6LargeSeen=1;\n";
    static const char large_js_suffix[] =
        "mark('large');\n"
        "fetch('/youtubei/v1/browse',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'})"
        ".then(function(r){return r.json();}).then(function(j){if(j.ok)mark('browse');})"
        ".catch(function(e){console.error('XV6-YTBOOT fetch '+e);});\n";
    static const char ytw_body[] =
        "<!doctype html><html><head><meta charset=utf-8>"
        "<title>xv6-ytwaterfall:boot</title>"
        "<script>"
        "window.__xv6Waterfall=[];"
        "function mark(x){if(__xv6Waterfall.indexOf(x)<0)__xv6Waterfall.push(x);"
        "document.title='xv6-ytwaterfall:'+__xv6Waterfall.join(',');"
        "console.log('XV6-YTWATERFALL '+__xv6Waterfall.join(','));done();}"
        "function done(){var need=['kevlar','webanimations','adapter','webcomponents','intersection','i18n','scheduler','spf','network','inline','body','app-connected','domcontent','load','browse'];"
        "if(need.every(function(x){return __xv6Waterfall.indexOf(x)>=0;})){document.title='xv6-ytwaterfall:PASS:'+__xv6Waterfall.join(',');console.log('XV6-YTWATERFALL PASS');}}"
        "document.addEventListener('DOMContentLoaded',function(){mark('domcontent');});"
        "window.addEventListener('load',function(){mark('load');});"
        "</script>"
        "<script src=/yt/kevlar.js></script>"
        "<script src=/yt/webanimations.js></script>"
        "<script src=/yt/adapter.js></script>"
        "<script src=/yt/webcomponents.js></script>"
        "<script src=/yt/intersection.js></script>"
        "<script src=/yt/i18n.js></script>"
        "<script src=/yt/scheduler.js></script>"
        "<script src=/yt/spf.js></script>"
        "<script src=/yt/network.js></script>"
        "<script>"
        "window.ytInitialData={contents:{twoColumnBrowseResultsRenderer:{tabs:[{tabRenderer:{content:{richGridRenderer:{contents:[{richItemRenderer:{content:{videoRenderer:{videoId:'xv6'}}}}]}}}}]}}};"
        "customElements.define('ytd-app',class extends HTMLElement{connectedCallback(){mark('app-connected');}});"
        "mark('inline');"
        "fetch('/youtubei/v1/browse',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({context:{client:{clientName:'WEB'}}})})"
        ".then(function(r){return r.json();}).then(function(j){if(j.ok)mark('browse');})"
        ".catch(function(e){console.error('XV6-YTWATERFALL fetch '+e);});"
        "</script></head><body><ytd-app><main id=feed><ytd-rich-grid-renderer>"
        "<ytd-rich-item-renderer><ytd-video-renderer><img src=/thumb.jpg></ytd-video-renderer></ytd-rich-item-renderer>"
        "</ytd-rich-grid-renderer></main></ytd-app>"
        "<script>mark('body');</script></body></html>";
    static const char ytw_kevlar_js[] =
        "window.ytcfg={data_:{EMERGENCY_BASE_URL:'http://127.0.0.1:18080',WEB_PLAYER_CONTEXT_CONFIGS:{WEB_PLAYER_CONTEXT_CONFIG_ID_KEVLAR_WATCH:{}}}};mark('kevlar');\n";
    static const char ytw_webanimations_js[] = "mark('webanimations');\n";
    static const char ytw_adapter_js[] = "mark('adapter');\n";
    static const char ytw_webcomponents_js[] = "window.ShadyCSS={};window.Polymer={};mark('webcomponents');\n";
    static const char ytw_intersection_js[] = "if(!window.IntersectionObserver)throw new Error('missing IntersectionObserver');mark('intersection');\n";
    static const char ytw_i18n_js[] = "window.yt={};mark('i18n');\n";
    static const char ytw_scheduler_js[] =
        "Promise.resolve().then(function(){mark('scheduler');});\n";
    static const char ytw_spf_js[] = "mark('spf');\n";
    static const char ytw_network_js[] = "mark('network');\n";
    static const char feature_gate_body[] =
        "<!doctype html><meta charset=utf-8>"
        "<title>xv6-feature-gates:boot</title><h1 id=out>boot</h1>"
        "<script>"
        "window.__xv6Feature=[];"
        "function mark(x){if(__xv6Feature.indexOf(x)<0)__xv6Feature.push(x);"
        "document.getElementById('out').textContent=__xv6Feature.join(',');"
        "document.title='xv6-feature-gates:'+__xv6Feature.join(',');"
        "console.log('XV6-FEATURE-GATES '+__xv6Feature.join(','));done();}"
        "function fail(x,e){document.title='xv6-feature-gates:FAIL:'+x;"
        "console.error('XV6-FEATURE-GATES FAIL '+x+' '+(e&&e.stack||e));}"
        "function done(){var need=['crypto','digest','offscreen','domcontent','load'];"
        "if(need.every(function(x){return __xv6Feature.indexOf(x)>=0;})){"
        "document.title='xv6-feature-gates:PASS:'+__xv6Feature.join(',');"
        "console.log('XV6-FEATURE-GATES PASS');}}"
        "document.addEventListener('DOMContentLoaded',function(){mark('domcontent');});"
        "window.addEventListener('load',function(){mark('load');});"
        "try{if(!window.crypto||!crypto.subtle)throw new Error('missing crypto.subtle');"
        "mark('crypto');crypto.subtle.digest('SHA-256',new Uint8Array([1,2,3])).then(function(){mark('digest');}).catch(function(e){fail('digest',e);});}"
        "catch(e){fail('crypto',e);}"
        "try{if(!window.OffscreenCanvas)throw new Error('missing OffscreenCanvas');"
        "var c=new OffscreenCanvas(8,8);var ctx=c.getContext('2d');ctx.fillRect(0,0,1,1);mark('offscreen');}"
        "catch(e){fail('offscreen',e);}"
        "</script>";
    static const char idle_browse_body[] =
        "<!doctype html><meta charset=utf-8>"
        "<title>xv6-idle-browse:boot</title><h1 id=out>boot</h1><ytd-app></ytd-app>"
        "<script>"
        "window.__xv6Idle=[];"
        "function mark(x){if(__xv6Idle.indexOf(x)<0)__xv6Idle.push(x);"
        "document.getElementById('out').textContent=__xv6Idle.join(',');"
        "document.title='xv6-idle-browse:'+__xv6Idle.join(',');"
        "console.log('XV6-IDLE-BROWSE '+__xv6Idle.join(','));done();}"
        "function fail(x,e){document.title='xv6-idle-browse:FAIL:'+x;"
        "console.error('XV6-IDLE-BROWSE FAIL '+x+' '+(e&&e.stack||e));}"
        "function done(){var need=['ce','idle-api','idle-run','browse','domcontent','load'];"
        "if(need.every(function(x){return __xv6Idle.indexOf(x)>=0;})){"
        "document.title='xv6-idle-browse:PASS:'+__xv6Idle.join(',');"
        "console.log('XV6-IDLE-BROWSE PASS');}}"
        "document.addEventListener('DOMContentLoaded',function(){mark('domcontent');});"
        "window.addEventListener('load',function(){mark('load');});"
        "customElements.define('ytd-app',class extends HTMLElement{connectedCallback(){mark('ce');}});"
        "try{if(typeof requestIdleCallback!=='function')throw new Error('missing requestIdleCallback');mark('idle-api');"
        "requestIdleCallback(function(deadline){try{if(!deadline||typeof deadline.timeRemaining!=='function')throw new Error('bad deadline');"
        "mark('idle-run');fetch('/youtubei/v1/browse',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'})"
        ".then(function(r){return r.json();}).then(function(j){if(j.ok)mark('browse');else fail('browse-status','bad json');})"
        ".catch(function(e){fail('browse-fetch',e);});}catch(e){fail('idle-run',e);}}, {timeout:500});}"
        "catch(e){fail('idle-api',e);}"
        "</script>";
    static const char compat_gate_body[] =
        "<!doctype html><meta charset=utf-8>"
        "<title>xv6-compat-gates:boot</title><h1 id=out>boot</h1><ytd-app></ytd-app>"
        "<script>"
        "window.__xv6Compat=[];"
        "function mark(x){if(__xv6Compat.indexOf(x)<0)__xv6Compat.push(x);"
        "document.getElementById('out').textContent=__xv6Compat.join(',');"
        "document.title='xv6-compat-gates:'+__xv6Compat.join(',');"
        "console.log('XV6-COMPAT-GATES '+__xv6Compat.join(','));done();}"
        "function fail(x,e){document.title='xv6-compat-gates:FAIL:'+x;"
        "console.error('XV6-COMPAT-GATES FAIL '+x+' '+(e&&e.stack||e));}"
        "function done(){var need=['ce','tt-api','tt-policy','ua-api','ua-high','idle-run','browse','domcontent','load'];"
        "if(need.every(function(x){return __xv6Compat.indexOf(x)>=0;})){"
        "document.title='xv6-compat-gates:PASS:'+__xv6Compat.join(',');"
        "console.log('XV6-COMPAT-GATES PASS');}}"
        "document.addEventListener('DOMContentLoaded',function(){mark('domcontent');});"
        "window.addEventListener('load',function(){mark('load');});"
        "customElements.define('ytd-app',class extends HTMLElement{connectedCallback(){mark('ce');}});"
        "try{if(!window.trustedTypes||typeof trustedTypes.createPolicy!=='function')throw new Error('missing trustedTypes');"
        "mark('tt-api');var p=trustedTypes.createPolicy('xv6',{createHTML:function(s){return String(s).replace('bad','good');},createScript:function(s){return String(s);},createScriptURL:function(s){return String(s);}});"
        "if(p.createHTML('bad')!=='good')throw new Error('bad Trusted Types policy');"
        "if(trustedTypes.createPolicy('xv6',{createHTML:function(s){return String(s);}})!==p)throw new Error('bad duplicate policy reuse');mark('tt-policy');}"
        "catch(e){fail('trusted-types',e);}"
        "try{if(!navigator.userAgentData||typeof navigator.userAgentData.getHighEntropyValues!=='function')throw new Error('missing userAgentData');"
        "mark('ua-api');navigator.userAgentData.getHighEntropyValues(['architecture','bitness','fullVersionList','platformVersion','uaFullVersion']).then(function(v){"
        "if(!v||!v.architecture||!v.bitness||!v.fullVersionList)throw new Error('bad high entropy values');mark('ua-high');}).catch(function(e){fail('ua-high',e);});}"
        "catch(e){fail('ua-api',e);}"
        "try{requestIdleCallback(function(){mark('idle-run');fetch('/youtubei/v1/browse',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'})"
        ".then(function(r){return r.json();}).then(function(j){if(j.ok)mark('browse');else fail('browse-status','bad json');})"
        ".catch(function(e){fail('browse-fetch',e);});},{timeout:500});}"
        "catch(e){fail('idle-run',e);}"
        "</script>";
    static const char json_body[] = "{\"ok\":true}\n";
    static const char xhr_body[] = "ok";
    static const char stream_body[] =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
        "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210\n";
    int coop = webkit_coop_smoke_enabled_by_cmdline();
    int js_smoke = webkit_js_smoke_enabled_by_cmdline();
    int ytboot_smoke = webkit_youtube_boot_smoke_enabled_by_cmdline();
    int ytw_smoke = webkit_youtube_waterfall_smoke_enabled_by_cmdline();
    int feature_gate_smoke = webkit_feature_gate_smoke_enabled_by_cmdline();
    int idle_browse_smoke = webkit_idle_browse_smoke_enabled_by_cmdline();
    int compat_gate_smoke = webkit_compat_gate_smoke_enabled_by_cmdline();
    char header[384];
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    struct sockaddr_in addr;

    if (fd < 0)
        _exit(1);

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(18080);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, 4) < 0)
        _exit(1);

    for (;;) {
        char req[2048];
        const char *path = "/";
        const char *body = plain_body;
        const char *ctype = "text/html";
        const char *extra = coop ? "Cross-Origin-Opener-Policy: same-origin\r\n" : "";
        size_t used = 0;
        int cfd = accept(fd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        while (used + 1 < sizeof(req)) {
            int n = read(cfd, req + used, sizeof(req) - used - 1);

            if (n <= 0)
                break;
            used += (size_t)n;
            req[used] = '\0';
            if (strstr(req, "\r\n\r\n"))
                break;
        }
        char *header_end = strstr(req, "\r\n\r\n");
        size_t body_seen = 0;
        size_t content_length = 0;

        if (header_end) {
            char *cl = strstr(req, "Content-Length:");

            if (!cl)
                cl = strstr(req, "content-length:");
            body_seen = used - (size_t)((header_end + 4) - req);
            if (cl) {
                cl = strchr(cl, ':');
                if (cl) {
                    cl++;
                    while (*cl == ' ' || *cl == '\t')
                        cl++;
                    content_length = strtoul(cl, NULL, 10);
                }
            }
        }
        while (body_seen < content_length) {
            char drain[512];
            size_t want = content_length - body_seen;
            int n;

            if (want > sizeof(drain))
                want = sizeof(drain);
            n = read(cfd, drain, want);
            if (n <= 0)
                break;
            body_seen += (size_t)n;
        }
        if (strncmp(req, "GET ", 4) == 0 || strncmp(req, "POST ", 5) == 0) {
            path = req + (req[0] == 'G' ? 4 : 5);
            char *end = strchr(path, ' ');
            if (end)
                *end = '\0';
        }
        if (ytboot_smoke && strcmp(path, "/large.js") == 0) {
            static const char pad[] =
                "                                                                ";
            const unsigned pad_bytes = 10 * 1024 * 1024;
            unsigned content_length = strlen(large_js_prefix) + pad_bytes +
                strlen(large_js_suffix);

            snprintf(header, sizeof(header),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/javascript\r\n"
                     "Content-Length: %u\r\n"
                     "%s"
                     "Connection: close\r\n\r\n",
                     content_length, extra);
            write(cfd, header, strlen(header));
            write(cfd, large_js_prefix, strlen(large_js_prefix));
            for (unsigned sent = 0; sent < pad_bytes; ) {
                unsigned n = sizeof(pad) - 1;

                if (n > pad_bytes - sent)
                    n = pad_bytes - sent;
                write(cfd, pad, n);
                sent += n;
            }
            write(cfd, large_js_suffix, strlen(large_js_suffix));
            close(cfd);
            continue;
        }
        if (compat_gate_smoke) {
            if (strcmp(path, "/youtubei/v1/browse") == 0) {
                body = json_body;
                ctype = "application/json";
            } else {
                body = compat_gate_body;
            }
        } else if (idle_browse_smoke) {
            if (strcmp(path, "/youtubei/v1/browse") == 0) {
                body = json_body;
                ctype = "application/json";
            } else {
                body = idle_browse_body;
            }
        } else if (feature_gate_smoke) {
            body = feature_gate_body;
        } else if (ytw_smoke) {
            if (strncmp(path, "/yt/", 4) == 0) {
                ctype = "text/javascript";
                if (strcmp(path, "/yt/kevlar.js") == 0) {
                    usleep(250000);
                    body = ytw_kevlar_js;
                } else if (strcmp(path, "/yt/webanimations.js") == 0) {
                    body = ytw_webanimations_js;
                } else if (strcmp(path, "/yt/adapter.js") == 0) {
                    body = ytw_adapter_js;
                } else if (strcmp(path, "/yt/webcomponents.js") == 0) {
                    usleep(150000);
                    body = ytw_webcomponents_js;
                } else if (strcmp(path, "/yt/intersection.js") == 0) {
                    body = ytw_intersection_js;
                } else if (strcmp(path, "/yt/i18n.js") == 0) {
                    body = ytw_i18n_js;
                } else if (strcmp(path, "/yt/scheduler.js") == 0) {
                    body = ytw_scheduler_js;
                } else if (strcmp(path, "/yt/spf.js") == 0) {
                    usleep(100000);
                    body = ytw_spf_js;
                } else if (strcmp(path, "/yt/network.js") == 0) {
                    body = ytw_network_js;
                } else {
                    body = "";
                }
            } else if (strcmp(path, "/youtubei/v1/browse") == 0) {
                body = json_body;
                ctype = "application/json";
            } else if (strcmp(path, "/thumb.jpg") == 0) {
                body = "";
                ctype = "image/jpeg";
            } else {
                body = ytw_body;
            }
        } else if (ytboot_smoke) {
            if (strcmp(path, "/small.js") == 0) {
                body = small_js;
                ctype = "text/javascript";
            } else if (strcmp(path, "/youtubei/v1/browse") == 0) {
                body = json_body;
                ctype = "application/json";
            } else {
                body = ytboot_body;
            }
        } else if (js_smoke) {
            if (strcmp(path, "/after.js") == 0) {
                body = after_js;
                ctype = "text/javascript";
            } else if (strcmp(path, "/json") == 0) {
                body = json_body;
                ctype = "application/json";
            } else if (strcmp(path, "/xhr") == 0) {
                body = xhr_body;
                ctype = "text/plain";
            } else if (strcmp(path, "/stream") == 0) {
                body = stream_body;
                ctype = "application/octet-stream";
            } else {
                body = js_body;
            }
        }
        snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %u\r\n"
                 "%s"
                 "Connection: close\r\n\r\n",
                 ctype, (unsigned)strlen(body), extra);
        write(cfd, header, strlen(header));
        write(cfd, body, strlen(body));
        close(cfd);
    }
    close(fd);
    _exit(0);
}

static pid_t launch_http_smoke_server(void)
{
    pid_t pid = fork();

    if (pid == 0)
        run_http_smoke_server();
    return pid;
}

static pid_t launch_client(const char *path, const char *name, const char *arg1,
                           const char *arg2, const char *arg3)
{
    int is_netsurf = strcmp(name, "netsurf") == 0;
    int is_minibrowser = strcmp(name, "MiniBrowser") == 0;
    int is_webkitgpusmoke = strcmp(name, "webkitgpusmoke") == 0;
    int is_mesa_gl = strcmp(name, "mesawlegl") == 0 ||
                     strcmp(name, "mesaglsmoke") == 0 ||
                     strcmp(name, "mesaeglinfo") == 0;
    int is_webkit = is_minibrowser || is_webkitgpusmoke;

    if (is_netsurf || is_webkit) {
        mkdir("/tmp/.cache", 0755);
        mkdir("/tmp/.cache/fontconfig", 0755);
        mkdir("/tmp/.local", 0755);
        mkdir("/tmp/.local/share", 0755);
        mkdir("/tmp/webkitgtk-4.1", 0755);
    }
    if (is_netsurf)
        mkdir("/.netsurf", 0755);

    pid_t pid = fork();
    if (pid == 0) {
        if (is_webkit)
            disable_child_coredumps();

        if (is_netsurf) {
            int logfd = open("/tmp/app_log.txt",
                             O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (logfd >= 0) {
                dup2(logfd, 1);
                dup2(logfd, 2);
                close(logfd);
            }
        }

        if (is_netsurf) {
            int fd = open("/.netsurf/Choices",
                          O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                const char *ch =
                    "ca_bundle:/share/netsurf/ca-bundle\n"
                    "homepage_url:file:///share/netsurf/welcome.html\n"
                    "curl_fetch_timeout:30\n";
                write(fd, ch, strlen(ch));
                close(fd);
            }
        }

        char *argv_default[] = {
            (char *)name,
            (char *)arg1,
            (char *)arg2,
            (char *)arg3,
            NULL,
        };
        if (arg1 == NULL)
            argv_default[1] = NULL;
        if (arg2 == NULL)
            argv_default[2] = NULL;
        if (arg3 == NULL)
            argv_default[3] = NULL;
        const char *minibrowser_url = arg1 ? arg1 : "https://www.google.com/";
        int minibrowser_youtube_compat =
            is_minibrowser && webkit_youtube_compat_url(minibrowser_url);
        const char *minibrowser_feature_flags =
            webkit_request_idle_disabled_by_cmdline() ?
            webkit_feature_flags_no_idle : webkit_feature_flags;
        char *argv_minibrowser[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            "--enable-javascript=false",
            "--enable-sandbox=false",
            "--enable-webgl=false",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_youtube[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            (char *)webkit_youtube_compat_user_agent,
            "--enable-javascript=false",
            "--enable-sandbox=false",
            "--enable-webgl=false",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_js[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            "--enable-sandbox=false",
            "--enable-webgl=false",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_js_youtube[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            (char *)webkit_youtube_compat_user_agent,
            "--enable-sandbox=false",
            "--enable-webgl=false",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_accel[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            "--enable-javascript=false",
            "--enable-sandbox=false",
            "--enable-webgl=false",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_accel_youtube[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            (char *)webkit_youtube_compat_user_agent,
            "--enable-javascript=false",
            "--enable-sandbox=false",
            "--enable-webgl=false",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_accel_js[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            "--enable-sandbox=false",
            "--enable-webgl=true",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_accel_js_youtube[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            (char *)webkit_youtube_compat_user_agent,
            "--enable-sandbox=false",
            "--enable-webgl=true",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_accel_webgl_js[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            "--enable-sandbox=false",
            "--enable-webgl=true",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_accel_webgl_js_youtube[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            (char *)webkit_youtube_compat_user_agent,
            "--enable-sandbox=false",
            "--enable-webgl=true",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *envp_default[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_HOME=/tmp/.local/share",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "GDK_GL=gles",
            "GDK_DPI_SCALE=1.0",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            NULL
        };
        char *envp_minibrowser[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "LD_LIBRARY_PATH=/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
            "LD_PRELOAD=/lib/libpng16.so.16:/lib/libxv6memshim.so",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "GDK_GL=gles",
            "GDK_DPI_SCALE=1.0",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "GIO_MODULE_DIR=/lib/gio/modules",
            "GIO_USE_TLS=gnutls",
            "GST_PLUGIN_SYSTEM_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_SCANNER_1_0=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_GL_PLATFORM=egl",
            "GST_GL_WINDOW=wayland",
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            "XV6_GUI_SESSION=1",
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            "WEBKIT_GST_DISABLE_GL_SINK=1",
            "WEBKIT_GST_DMABUF_SINK_DISABLED=1",
            "WEBKIT_GST_USE_VIDEOCONVERT_SCALE=1",
            "WEBKIT_GST_MAX_AVC1_RESOLUTION=720P",
            "WEBKIT_DISABLE_COMPOSITING_MODE=1",
            "WEBKIT_XV6_DISABLE_COMPOSITING_UPDATE=1",
            "EPOXY_XV6_ALLOW_MISSING=1",
            "WEBKIT_XV6_SKIP_RULE_FEATURES=1",
            "WEBKIT_XV6_SKIP_INITIAL_EMPTY_RENDER=1",
            "SOUP_FORCE_HTTP1=1",
            NULL
        };
        char *envp_minibrowser_accel[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "LD_LIBRARY_PATH=/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
            "LD_PRELOAD=/lib/libpng16.so.16:/lib/libxv6memshim.so",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_HOME=/tmp/.local/share",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "GDK_GL=gles",
            "GDK_DPI_SCALE=1.0",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "GIO_MODULE_DIR=/lib/gio/modules",
            "GIO_USE_TLS=gnutls",
            "GST_PLUGIN_SYSTEM_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_SCANNER_1_0=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_GL_PLATFORM=egl",
            "GST_GL_WINDOW=wayland",
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            "XV6_GUI_SESSION=1",
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            "LIBGL_ALWAYS_SOFTWARE=0",
            "GALLIUM_DRIVER=virgl",
            "EGL_PLATFORM=wayland",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "SOUP_FORCE_HTTP1=1",
            "EPOXY_XV6_ALLOW_MISSING=1",
            NULL
        };
        char *envp_minibrowser_accel_sw[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "LD_LIBRARY_PATH=/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
            "LD_PRELOAD=/lib/libpng16.so.16:/lib/libxv6memshim.so",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_HOME=/tmp/.local/share",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "GDK_GL=gles",
            "GDK_DPI_SCALE=1.0",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "GIO_MODULE_DIR=/lib/gio/modules",
            "GIO_USE_TLS=gnutls",
            "GST_PLUGIN_SYSTEM_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_SCANNER_1_0=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_GL_PLATFORM=egl",
            "GST_GL_WINDOW=wayland",
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            "XV6_GUI_SESSION=1",
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            "WEBKIT_GST_DISABLE_GL_SINK=1",
            "WEBKIT_GST_DMABUF_SINK_DISABLED=1",
            "WEBKIT_GST_USE_VIDEOCONVERT_SCALE=1",
            "WEBKIT_GST_MAX_AVC1_RESOLUTION=720P",
            "LIBGL_ALWAYS_SOFTWARE=1",
            "EGL_PLATFORM=wayland",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "WEBKIT_XV6_DISABLE_BCG_SWITCH=1",
            "WEBKIT_XV6_SKIP_RULE_FEATURES=1",
            "SOUP_FORCE_HTTP1=1",
            "EPOXY_XV6_ALLOW_MISSING=1",
            NULL
        };
        char *envp_mesa_accel[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "LIBGL_ALWAYS_SOFTWARE=0",
            "GALLIUM_DRIVER=virgl",
            "EGL_PLATFORM=wayland",
            NULL
        };
        char *envp_mesa_accel_sw[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "LIBGL_ALWAYS_SOFTWARE=1",
            "EGL_PLATFORM=wayland",
            "MESA_LOADER_DRIVER_OVERRIDE=softpipe",
            "LIBGL_DRIVERS_PATH=/usr/lib/x86_64-linux-gnu/dri",
            NULL
        };
        int minibrowser_accel =
            is_minibrowser && webkit_accel_enabled_by_cmdline();
        int minibrowser_js =
            is_minibrowser && !webkit_js_disabled_by_cmdline();
        int minibrowser_webgl_smoke =
            is_minibrowser && webkit_webgl_smoke_enabled_by_cmdline();
        int webkit_accel =
            (is_minibrowser && minibrowser_accel) || is_webkitgpusmoke;
        int virgl_available = xv6_virgl_available();
        char **argv_exec = argv_default;
        if (is_webkitgpusmoke)
            webkit_accel = 0;
        if (is_minibrowser) {
            if (minibrowser_accel) {
                if (minibrowser_js) {
                    if (minibrowser_webgl_smoke)
                        argv_exec = minibrowser_youtube_compat ?
                            argv_minibrowser_accel_webgl_js_youtube :
                            argv_minibrowser_accel_webgl_js;
                    else
                        argv_exec = minibrowser_youtube_compat ?
                            argv_minibrowser_accel_js_youtube :
                            argv_minibrowser_accel_js;
                } else {
                    argv_exec = minibrowser_youtube_compat ?
                        argv_minibrowser_accel_youtube :
                        argv_minibrowser_accel;
                }
            } else {
                argv_exec = minibrowser_js ?
                    (minibrowser_youtube_compat ?
                         argv_minibrowser_js_youtube :
                         argv_minibrowser_js) :
                    (minibrowser_youtube_compat ?
                         argv_minibrowser_youtube :
                         argv_minibrowser);
            }
        }
        errno = 0;
        execve(path,
               argv_exec,
               is_webkit ?
                    (webkit_accel ?
                         (virgl_available ? envp_minibrowser_accel :
                                            envp_minibrowser_accel_sw) :
                         envp_minibrowser) :
                    (is_mesa_gl ?
                         (virgl_available ? envp_mesa_accel :
                                            envp_mesa_accel_sw) :
                         envp_default));
        fprintf(stderr, "%s: execve failed errno=%d (%s)\n", path, errno,
                errno ? strerror(errno) : "no errno from kernel");
        _exit(127);
    }
    if (pid > 0)
        setpgid(pid, pid);
    return pid;
}

static void kill_and_reap(pid_t *pidp)
{
    if (*pidp > 0) {
        kill(-*pidp, SIGTERM);
        kill(*pidp, SIGTERM);
        waitpid(*pidp, NULL, 0);
        *pidp = 0;
    }
}

static void cleanup(void)
{
    kill_and_reap(&client_pid);
    kill_and_reap(&httpd_pid);
    kill_and_reap(&wlcomp_pid);
}

static int token_is_disabled(const char *cmdline, const char *key)
{
    size_t key_len = strlen(key);
    const char *p = cmdline;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=' &&
            p[key_len + 1] == '0' &&
            (p[key_len + 2] == '\0' || p[key_len + 2] == ' ' ||
             p[key_len + 2] == '\t' || p[key_len + 2] == '\n'))
            return 1;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
    }
    return 0;
}

static int token_is_enabled(const char *cmdline, const char *key)
{
    size_t key_len = strlen(key);
    const char *p = cmdline;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=' &&
            p[key_len + 1] == '1' &&
            (p[key_len + 2] == '\0' || p[key_len + 2] == ' ' ||
             p[key_len + 2] == '\t' || p[key_len + 2] == '\n'))
            return 1;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
    }
    return 0;
}

static int read_cmdline(char *buf, size_t buf_size)
{
    int fd = open("/proc/cmdline", O_RDONLY);
    size_t total = 0;
    if (fd < 0) {
        fprintf(stderr, "[desktop] /proc/cmdline unavailable\n");
        return -1;
    }

    while (total + 1 < buf_size) {
        ssize_t n = read(fd, buf + total, buf_size - total - 1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            fprintf(stderr, "[desktop] /proc/cmdline read failed: %s\n",
                    strerror(errno));
            return -1;
        }
        if (n == 0)
            break;
        total += (size_t)n;
    }
    close(fd);
    if (total == 0) {
        fprintf(stderr, "[desktop] /proc/cmdline empty\n");
        return -1;
    }
    buf[total] = '\0';
    return 0;
}

static int cmdline_int_value(const char *cmdline, const char *key, int fallback)
{
    size_t key_len = strlen(key);
    const char *p = cmdline;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            int value = atoi(p + key_len + 1);

            return value > 0 ? value : fallback;
        }
        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
    }
    return fallback;
}

static int netsurf_disabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_disabled(buf, "netsurf");
}

static int desktop_disabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_disabled(buf, "desktop");
}

static int webkit_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return !token_is_disabled(buf, "webkit") && strstr(buf, "webkit=1") != NULL;
}

static int webkit_accel_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_accel");
}

static int webkit_gpu_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_gpu_smoke");
}

static int webkit_webgl_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_webgl_smoke");
}

static int webkit_api_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_api_smoke");
}

static int webkit_http_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_http_smoke");
}

static int webkit_coop_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_coop_smoke");
}

static int webkit_js_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_js_smoke");
}

static int webkit_youtube_boot_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_youtube_boot_smoke");
}

static int webkit_youtube_waterfall_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_youtube_waterfall_smoke");
}

static int webkit_youtube_compat_disabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_disabled(buf, "webkit_youtube_compat");
}

static int webkit_request_idle_disabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_disabled(buf, "webkit_request_idle");
}

static int webkit_feature_gate_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_feature_gate_smoke");
}

static int webkit_idle_browse_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_idle_browse_smoke");
}

static int webkit_compat_gate_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_compat_gate_smoke");
}

static int webkit_js_disabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_disabled(buf, "webkit_js");
}

static int webkit_reopen_count_from_cmdline(void)
{
    char buf[512];
    int count = 1;

    if (read_cmdline(buf, sizeof(buf)) == 0)
        count = cmdline_int_value(buf, "webkit_reopen", count);
    if (count < 1)
        count = 1;
    if (count > 10)
        count = 10;
    return count;
}

static int webkit_timeout_ms_from_cmdline(int fallback)
{
    char buf[512];
    int timeout_ms = fallback;

    if (read_cmdline(buf, sizeof(buf)) == 0) {
        const char *key = "webkit_timeout_ms";
        size_t key_len = strlen(key);
        const char *p = buf;

        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\n')
                p++;
            if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
                timeout_ms = atoi(p + key_len + 1);
                break;
            }
            while (*p && *p != ' ' && *p != '\t' && *p != '\n')
                p++;
        }
    }
    if (timeout_ms < 0)
        timeout_ms = 0;
    if (timeout_ms > 600000)
        timeout_ms = 600000;
    return timeout_ms;
}

static int webkit_url_has_scheme(const char *s)
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

static int webkit_url_looks_like_host(const char *s)
{
    int saw_dot = 0;
    size_t i;

    if (!s || !s[0])
        return 0;
    for (i = 0; s[i] && s[i] != '/' && s[i] != '?' && s[i] != '#'; i++) {
        if (s[i] == '.')
            saw_dot = 1;
        if (!(s[i] == '.' || s[i] == '-' ||
              (s[i] >= '0' && s[i] <= '9') ||
              (s[i] >= 'A' && s[i] <= 'Z') ||
              (s[i] >= 'a' && s[i] <= 'z')))
            return 0;
    }
    return saw_dot || strncmp(s, "localhost", 9) == 0;
}

static void copy_prefixed_url(char *out, size_t out_size, const char *prefix, const char *value)
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

static void normalize_webkit_url(const char *in, char *out, size_t out_size)
{
    char tmp[WEBKIT_URL_MAX];
    size_t len = 0;

    if (out_size == 0)
        return;
    if (!in)
        in = "";
    while (*in == ' ' || *in == '\t' || *in == '\n')
        in++;
    while (in[len] && in[len] != ' ' && in[len] != '\t' &&
           in[len] != '\n' && len + 1 < sizeof(tmp)) {
        tmp[len] = in[len];
        len++;
    }
    tmp[len] = '\0';
    out[0] = '\0';

    if (tmp[0] == '\0') {
        snprintf(out, out_size, WEBKIT_DEFAULT_URL);
    } else if (strncmp(tmp, "http//", 6) == 0) {
        copy_prefixed_url(out, out_size, "http://", tmp + 6);
    } else if (strncmp(tmp, "https//", 7) == 0) {
        copy_prefixed_url(out, out_size, "https://", tmp + 7);
    } else if (strncmp(tmp, "http:/", 6) == 0 &&
               strncmp(tmp, "http://", 7) != 0) {
        copy_prefixed_url(out, out_size, "http://", tmp + 6);
    } else if (strncmp(tmp, "https:/", 7) == 0 &&
               strncmp(tmp, "https://", 8) != 0) {
        copy_prefixed_url(out, out_size, "https://", tmp + 7);
    } else if (webkit_url_has_scheme(tmp)) {
        snprintf(out, out_size, "%s", tmp);
    } else if (tmp[0] == '/') {
        copy_prefixed_url(out, out_size, "file://", tmp);
    } else if (webkit_url_looks_like_host(tmp)) {
        copy_prefixed_url(out, out_size, "https://", tmp);
    } else {
        snprintf(out, out_size, "%s", tmp);
    }
}

static void webkit_url_from_cmdline(char *out, size_t out_size)
{
    char buf[2048];
    const char *key = "webkit_url=";
    size_t key_len = strlen(key);
    const char *p;

    if (out_size == 0)
        return;

    if (webkit_webgl_smoke_enabled_by_cmdline()) {
        snprintf(out, out_size, "file:///share/webkit/gpu-webgl-smoke.html");
        return;
    }
    if (webkit_gpu_smoke_enabled_by_cmdline()) {
        snprintf(out, out_size, "file:///share/webkit/gpu-smoke.html");
        return;
    }
    if (webkit_http_smoke_enabled_by_cmdline()) {
        snprintf(out, out_size, "http://127.0.0.1:18080/");
        return;
    }

    if (read_cmdline(buf, sizeof(buf)) < 0) {
        normalize_webkit_url(WEBKIT_DEFAULT_URL, out, out_size);
        return;
    }

    p = buf;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (strncmp(p, key, key_len) == 0) {
            size_t i = 0;
            p += key_len;
            while (p[i] && p[i] != ' ' && p[i] != '\t' && p[i] != '\n' &&
                   i + 1 < out_size) {
                out[i] = p[i];
                i++;
            }
            out[i] = '\0';
            if (out[0]) {
                normalize_webkit_url(out, out, out_size);
                return;
            }
            break;
        }
        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
    }

    normalize_webkit_url(WEBKIT_DEFAULT_URL, out, out_size);
}

static int glsmoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return !token_is_disabled(buf, "glsmoke") &&
           strstr(buf, "glsmoke=1") != NULL;
}

static int glsmoke_compat_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "glsmoke_compat");
}

static int glsmoke_native_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "glsmoke_native");
}

static int glsmoke_demo_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "glsmoke_demo");
}

static void glsmoke_args_from_cmdline(char *frames_arg, size_t frames_size,
                                      char *loops_arg, size_t loops_size,
                                      char *resize_arg, size_t resize_size)
{
    char buf[512];
    int frames = 120;
    int loops = 1;
    int resize_every = 0;

    if (read_cmdline(buf, sizeof(buf)) == 0) {
        frames = cmdline_int_value(buf, "glsmoke_frames", frames);
        loops = cmdline_int_value(buf, "glsmoke_loops", loops);
        resize_every = cmdline_int_value(buf, "glsmoke_resize_every",
                                         resize_every);
    }
    snprintf(frames_arg, frames_size, "--frames=%d", frames);
    snprintf(loops_arg, loops_size, "--loops=%d", loops);
    if (resize_every > 0)
        snprintf(resize_arg, resize_size, "--resize-every=%d", resize_every);
    else if (resize_size > 0)
        resize_arg[0] = '\0';
}

int main(void)
{
    signal(SIGINT,  sighandler);
    signal(SIGTERM, sighandler);

    if (desktop_disabled_by_cmdline()) {
        fprintf(stderr, "[desktop] disabled by cmdline\n");
        return 0;
    }

    fprintf(stderr, "[desktop] starting Wayland session\n");

    /* 1. Launch compositor */
    wlcomp_pid = launch_wlcomp();
    if (wlcomp_pid < 0) {
        perror("[desktop] fork wlcomp");
        return 1;
    }
    fprintf(stderr, "[desktop] wlcomp pid=%d\n", wlcomp_pid);

    /* 2. Wait for Wayland socket */
    if (wait_for_socket() < 0) {
        fprintf(stderr, "[desktop] timed out waiting for %s\n",
                WAYLAND_SOCKET_PATH);
        cleanup();
        return 1;
    }

    /* 3. Launch the requested Wayland client. */
    if (glsmoke_enabled_by_cmdline()) {
        char frames_arg[32];
        char loops_arg[32];
        char resize_arg[32];
        int compat = glsmoke_compat_by_cmdline();
        int native = !compat && glsmoke_native_by_cmdline();
        int demo = !compat && glsmoke_demo_by_cmdline();
        const char *client_path = compat ? "/bin/glsmoke" :
                                  demo ? "/bin/mesaglsmoke" :
                                  native ? "/bin/mesawlegl" :
                                           "/bin/mesaglsmoke";
        const char *client_name = compat ? "glsmoke" :
                                          demo ? "mesaglsmoke" :
                                          native ? "mesawlegl" : "mesaglsmoke";

        glsmoke_args_from_cmdline(frames_arg, sizeof(frames_arg), loops_arg,
                                  sizeof(loops_arg), resize_arg,
                                  sizeof(resize_arg));
        client_pid = demo ?
            launch_client(client_path, client_name, "--demo", NULL, NULL) :
            launch_client(client_path, client_name, frames_arg, loops_arg,
                          resize_arg[0] ? resize_arg : NULL);
        if (client_pid < 0) {
            perror("[desktop] fork GL smoke");
            cleanup();
            return 1;
        }
        fprintf(stderr, "[desktop] %s pid=%d %s %s %s\n", client_name,
                client_pid, demo ? "--demo" : frames_arg,
                demo ? "" : loops_arg,
                demo ? "" : resize_arg);
        if (webkit_enabled_by_cmdline()) {
            while (g_running && client_pid > 0) {
                int status;
                pid_t exited = waitpid(-1, &status, WNOHANG);
                if (exited == wlcomp_pid) {
                    fprintf(stderr, "[desktop] wlcomp exited (status %d)\n",
                            WEXITSTATUS(status));
                    wlcomp_pid = 0;
                    cleanup();
                    return 1;
                }
                if (exited == client_pid) {
                    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                        fprintf(stderr,
                                "[desktop] GL smoke exited (status %d)\n",
                                WIFEXITED(status) ? WEXITSTATUS(status) :
                                                    status);
                    }
                    client_pid = 0;
                    break;
                }
                usleep(100000);
            }
        }
    }

    if (webkit_enabled_by_cmdline()) {
        int accel = webkit_accel_enabled_by_cmdline();
        int webkit_reopen_left = webkit_reopen_count_from_cmdline();
        int webkit_api_smoke = webkit_api_smoke_enabled_by_cmdline();
        int webkit_timeout_ms = webkit_timeout_ms_from_cmdline(
            webkit_api_smoke ? 15000 : 0);
        long long launch_ms = 0;
        const char *webkit_path = webkit_api_smoke ?
            "/bin/webkitgpusmoke" : "/libexec/webkit2gtk-4.1/MiniBrowser";
        const char *webkit_name = webkit_api_smoke ?
            "webkitgpusmoke" : "MiniBrowser";
        char webkit_timeout_arg[24];
        const char *webkit_timeout = NULL;
        char webkit_url[WEBKIT_URL_MAX];

        if (webkit_http_smoke_enabled_by_cmdline()) {
            httpd_pid = launch_http_smoke_server();
            if (httpd_pid < 0) {
                perror("[desktop] fork HTTP smoke server");
                cleanup();
                return 1;
            }
            fprintf(stderr, "[desktop] HTTP smoke server pid=%d\n",
                    httpd_pid);
            usleep(100000);
        }

        if (webkit_api_smoke) {
            snprintf(webkit_timeout_arg, sizeof(webkit_timeout_arg), "%d",
                     webkit_timeout_ms);
            webkit_timeout = webkit_timeout_arg;
        }
        webkit_url_from_cmdline(webkit_url, sizeof(webkit_url));
        if (url_needs_network_wait(webkit_url)) {
            fprintf(stderr,
                    "[desktop] waiting for network before WebKit URL %s\n",
                    webkit_url);
            usleep(WEBKIT_NET_WAIT_US);
        }
        client_pid = launch_client(webkit_path, webkit_name, webkit_url,
                                   webkit_timeout, NULL);
        if (client_pid < 0) {
            perror("[desktop] fork WebKit");
            cleanup();
            return 1;
        }
        fprintf(stderr,
                "[desktop] %s pid=%d accel=%d reopen_left=%d timeout_ms=%d "
                "url=%s\n",
                webkit_name, client_pid, accel, webkit_reopen_left,
                webkit_timeout_ms, webkit_url);
        launch_ms = monotonic_ms();

        while (g_running) {
            int status;
            long long now_ms;
            pid_t exited = waitpid(-1, &status, WNOHANG);
            if (exited > 0) {
                if (exited == wlcomp_pid) {
                    fprintf(stderr, "[desktop] wlcomp exited (status %d)\n",
                            WEXITSTATUS(status));
                    wlcomp_pid = 0;
                    break;
                }
                if (exited == client_pid) {
                    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                        fprintf(stderr,
                                "[desktop] client exited (status %d)\n",
                                WIFEXITED(status) ? WEXITSTATUS(status) :
                                                    status);
                    }
                    client_pid = 0;
                    if (webkit_reopen_left > 1) {
                        webkit_reopen_left--;
                        client_pid = launch_client(webkit_path, webkit_name,
                                                   webkit_url, webkit_timeout,
                                                   NULL);
                        if (client_pid < 0) {
                            perror("[desktop] refork WebKit");
                            break;
                        }
                        fprintf(stderr,
                                "[desktop] relaunched %s pid=%d "
                                "reopen_left=%d url=%s\n",
                                webkit_name, client_pid, webkit_reopen_left,
                                webkit_url);
                        launch_ms = monotonic_ms();
                    } else if (webkit_api_smoke) {
                        fprintf(stderr,
                                "[desktop] WebKit API reopen smoke complete\n");
                        break;
                    }
                }
            }
            usleep(100000);
            now_ms = monotonic_ms();
            if (webkit_timeout_ms > 0 && client_pid > 0 &&
                now_ms - launch_ms >= webkit_timeout_ms) {
                fprintf(stderr,
                        "[desktop] WebKit timeout reached, closing pid=%d\n",
                        client_pid);
                kill_and_reap(&client_pid);
                if (webkit_reopen_left > 1) {
                    webkit_reopen_left--;
                    client_pid = launch_client(webkit_path, webkit_name,
                                               webkit_url, webkit_timeout,
                                               NULL);
                    if (client_pid < 0) {
                        perror("[desktop] refork WebKit after timeout");
                        break;
                    }
                    fprintf(stderr,
                            "[desktop] relaunched %s pid=%d reopen_left=%d "
                            "url=%s\n",
                            webkit_name, client_pid, webkit_reopen_left,
                            webkit_url);
                    launch_ms = monotonic_ms();
                } else {
                    fprintf(stderr,
                            "[desktop] WebKit timeout smoke complete\n");
                    break;
                }
            }
        }
        fprintf(stderr, "[desktop] shutting down\n");
        cleanup();
        return 0;
    } else if (netsurf_disabled_by_cmdline()) {
        client_pid = 0;
        fprintf(stderr, "[desktop] netsurf disabled by cmdline\n");
    } else {
        client_pid = launch_client("/bin/netsurf", "netsurf", NULL, NULL, NULL);
        if (client_pid < 0) {
            perror("[desktop] fork netsurf");
            cleanup();
            return 1;
        }
        fprintf(stderr, "[desktop] netsurf pid=%d\n", client_pid);
    }

    /* 4. Supervise compositor and client */
    while (g_running) {
        int status;
        pid_t exited = waitpid(-1, &status, WNOHANG);
        if (exited > 0) {
            if (exited == wlcomp_pid) {
                fprintf(stderr, "[desktop] wlcomp exited (status %d)\n",
                        WEXITSTATUS(status));
                wlcomp_pid = 0;
                break;  /* compositor gone → session over */
            } else if (exited == client_pid) {
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    fprintf(stderr, "[desktop] client exited (status %d)\n",
                            WIFEXITED(status) ? WEXITSTATUS(status) : status);
                }
                client_pid = 0;
            }
        }
        usleep(100000);  /* 100 ms poll */
    }

    fprintf(stderr, "[desktop] shutting down\n");
    cleanup();
    return 0;
}
