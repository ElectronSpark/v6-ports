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
        "mark('fetch-start');fetch('/json').then(function(r){mark('fetch-response');return r.json();}).then(function(j){if(j.ok)hit('fetch');else mark('fetch-bad-json');}).catch(function(e){mark('fetch-error');console.error('XV6-JS-SMOKE fetch '+e);});"
        "mark('stream-start');fetch('/stream').then(function(r){mark('stream-response');if(!r.body||typeof r.body.getReader!=='function')throw new Error('missing body reader');var rd=r.body.getReader();var total=0;function pump(){return rd.read().then(function(x){if(x.done){if(total>0)hit('fetch-stream');else throw new Error('empty stream');return;}total+=x.value?x.value.byteLength:0;mark('stream-chunk');return pump();});}return pump();}).catch(function(e){mark('stream-error');console.error('XV6-JS-SMOKE fetch-stream '+e);});"
        "mark('xhr-start');var x=new XMLHttpRequest();x.onload=function(){mark('xhr-load');if(x.responseText==='ok')hit('xhr');else mark('xhr-bad');};x.onerror=function(){mark('xhr-error');console.error('XV6-JS-SMOKE xhr error');};x.open('GET','/xhr');x.send();"
        "</script><script src=/after.js></script>";
    static const char after_js[] = "hit('external');\n";
    static const char simple_inline_body[] =
        "<!doctype html><title>xv6-simple:boot</title>"
        "<script>document.title='xv6-simple:PASS';console.log('XV6-SIMPLE PASS');</script>";
    static const char media_init_body[] =
        "<!doctype html><meta charset=utf-8>"
        "<title>xv6-media-init:boot</title><pre id=out>boot</pre>"
        "<script>"
        "(function(){var out=document.getElementById('out'),steps=[];"
        "function mark(s){steps.push(s);document.title='xv6-media-init:'+steps.join(',');"
        "out.textContent=steps.join('\\n');console.log('XV6-MEDIA-INIT '+steps.join(','));}"
        "function safe(s,f){mark('before-'+s);try{mark('after-'+s+':'+f());}"
        "catch(e){mark('throw-'+s+':'+e.name+':'+e.message);}}"
        "mark('script');"
        "safe('create-video',function(){return typeof document.createElement('video');});"
        "safe('create-audio',function(){return typeof document.createElement('audio');});"
        "var video=document.createElement('video');"
        "safe('canplay-mp4',function(){return video.canPlayType('video/mp4');});"
        "safe('canplay-avc',function(){return video.canPlayType('video/mp4; codecs=\"avc1.42E01E, mp4a.40.2\"');});"
        "safe('mediasource-type',function(){return typeof window.MediaSource;});"
        "safe('mse-mp4',function(){return window.MediaSource&&MediaSource.isTypeSupported?String(MediaSource.isTypeSupported('video/mp4; codecs=\"avc1.42E01E, mp4a.40.2\"')):'missing';});"
        "safe('attach-src',function(){video.muted=true;video.playsInline=true;video.src='test-mse.mp4';return video.readyState+'/'+video.networkState;});"
        "setTimeout(function(){mark('timeout:'+video.readyState+'/'+video.networkState+':err='+(video.error?video.error.code:0));},500);"
        "})();</script>";
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

    if (fd < 0) {
        fprintf(stderr, "[desktop] HTTP smoke socket failed errno=%d (%s)\n",
                errno, strerror(errno));
        _exit(1);
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(18080);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[desktop] HTTP smoke bind failed errno=%d (%s)\n",
                errno, strerror(errno));
        _exit(1);
    }
    if (listen(fd, 4) < 0) {
        fprintf(stderr, "[desktop] HTTP smoke listen failed errno=%d (%s)\n",
                errno, strerror(errno));
        _exit(1);
    }
    fprintf(stderr, "[desktop] HTTP smoke listening on 127.0.0.1:18080\n");

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
            fprintf(stderr, "[desktop] HTTP smoke accept failed errno=%d (%s)\n",
                    errno, strerror(errno));
            break;
        }
        http_smoke_request_count++;
        {
            char count_buf[32];
            int count_fd = open("/tmp/http-smoke-count",
                                O_WRONLY | O_CREAT | O_TRUNC, 0644);

            if (count_fd >= 0) {
                int count_len = snprintf(count_buf, sizeof(count_buf), "%d\n",
                                         http_smoke_request_count);
                if (count_len > 0)
                    write(count_fd, count_buf, (size_t)count_len);
                close(count_fd);
            }
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
            char *query = strchr(path, '?');
            if (query)
                *query = '\0';
        }
        fprintf(stderr, "[desktop] HTTP smoke request %s\n", path);
        if (http_try_serve_webkit_file(cfd, path, extra)) {
            close(cfd);
            continue;
        }
        if (strcmp(path, "/simple-inline.html") == 0) {
            body = simple_inline_body;
            ctype = "text/html";
        } else if (strcmp(path, "/media-init-inline.html") == 0) {
            body = media_init_body;
            ctype = "text/html";
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
            if (write_all_fd(cfd, header, strlen(header)) != 0 ||
                write_all_fd(cfd, large_js_prefix,
                             strlen(large_js_prefix)) != 0) {
                close(cfd);
                continue;
            }
            for (unsigned sent = 0; sent < pad_bytes; ) {
                unsigned n = sizeof(pad) - 1;

                if (n > pad_bytes - sent)
                    n = pad_bytes - sent;
                if (write_all_fd(cfd, pad, n) != 0)
                    break;
                sent += n;
            }
            (void)write_all_fd(cfd, large_js_suffix,
                               strlen(large_js_suffix));
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
        (void)write_all_fd(cfd, header, strlen(header));
        (void)write_all_fd(cfd, body, strlen(body));
        close(cfd);
    }
    close(fd);
    _exit(0);
}

static int write_all_fd(int fd, const void *buf, size_t len)
{
    const char *p = buf;

    while (len > 0) {
        ssize_t n = write(fd, p, len);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0) {
            errno = EIO;
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static const char *http_content_type_for_path(const char *path)
{
    const char *ext = strrchr(path, '.');

    if (!ext)
        return "application/octet-stream";
    if (strcmp(ext, ".html") == 0)
        return "text/html";
    if (strcmp(ext, ".js") == 0)
        return "text/javascript";
    if (strcmp(ext, ".json") == 0)
        return "application/json";
    if (strcmp(ext, ".mp4") == 0)
        return "video/mp4";
    if (strcmp(ext, ".webm") == 0)
        return "video/webm";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(ext, ".png") == 0)
        return "image/png";
    if (strcmp(ext, ".css") == 0)
        return "text/css";
    return "application/octet-stream";
}

static int http_try_serve_webkit_file(int cfd, const char *path,
                                      const char *extra)
{
    char fs_path[256];
    char header[384];
    char buf[8192];
    struct stat st;
    int fd;

    if (!path || path[0] != '/' || strcmp(path, "/") == 0 ||
        strstr(path, "..") != NULL)
        return 0;
    if (snprintf(fs_path, sizeof(fs_path), "/share/webkit%s", path) >=
        (int)sizeof(fs_path))
        return 0;
    fd = open(fs_path, O_RDONLY);
    if (fd < 0)
        return 0;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return 0;
    }

    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %u\r\n"
             "%s"
             "Connection: close\r\n\r\n",
             http_content_type_for_path(fs_path), (unsigned)st.st_size, extra);
    if (write_all_fd(cfd, header, strlen(header)) < 0) {
        fprintf(stderr, "[desktop] HTTP file header write failed %s errno=%d (%s)\n",
                path, errno, strerror(errno));
        close(fd);
        return 1;
    }
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));

        if (n < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "[desktop] HTTP file read failed %s errno=%d (%s)\n",
                    path, errno, strerror(errno));
            break;
        }
        if (n == 0)
            break;
        if (write_all_fd(cfd, buf, (size_t)n) < 0) {
            fprintf(stderr, "[desktop] HTTP file body write failed %s errno=%d (%s)\n",
                    path, errno, strerror(errno));
            break;
        }
    }
    close(fd);
    fprintf(stderr, "[desktop] HTTP file served %s bytes=%u\n",
            path, (unsigned)st.st_size);
    return 1;
}

static pid_t launch_http_smoke_server(void)
{
    pid_t pid = fork();

    if (pid == 0)
        run_http_smoke_server();
    return pid;
}

static void http_smoke_self_probe(const char *path)
{
    char req[512];
    char buf[128];
    struct sockaddr_in addr;
    int fd;
    ssize_t n;

    if (!path || path[0] != '/')
        path = "/";
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "[desktop] HTTP smoke self-probe socket failed errno=%d (%s)\n",
                errno, strerror(errno));
        return;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(18080);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[desktop] HTTP smoke self-probe connect failed errno=%d (%s)\n",
                errno, strerror(errno));
        close(fd);
        return;
    }
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",
             path);
    if (write_all_fd(fd, req, strlen(req)) < 0) {
        fprintf(stderr, "[desktop] HTTP smoke self-probe write failed errno=%d (%s)\n",
                errno, strerror(errno));
        close(fd);
        return;
    }
    n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        fprintf(stderr, "[desktop] HTTP smoke self-probe response %.64s\n",
                buf);
    } else {
        fprintf(stderr, "[desktop] HTTP smoke self-probe read failed n=%zd errno=%d (%s)\n",
                n, errno, strerror(errno));
    }
    close(fd);
}

static void webkit_read_line(const char *path, char *buf, size_t buf_size)
{
    int fd;
    ssize_t n;

    if (buf_size == 0)
        return;
    buf[0] = '\0';
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return;
    n = read(fd, buf, buf_size - 1);
    close(fd);
    if (n <= 0)
        return;
    buf[n] = '\0';
    for (ssize_t i = 0; i < n; i++) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            buf[i] = '\0';
            break;
        }
    }
}

static void webkit_print_runtime_probe(void)
{
    char title[128];
    char count[32];
    struct stat log_st;
    long log_size = -1;
    static long last_log_size = -2;
    static int stable_log_samples;
    static int excerpt_printed;

    webkit_read_line("/tmp/webkit-title", title, sizeof(title));
    webkit_read_line("/tmp/http-smoke-count", count, sizeof(count));
    if (stat("/tmp/webkit_log.txt", &log_st) == 0)
        log_size = (long)log_st.st_size;
    fprintf(stderr,
            "[desktop] WebKit probe title='%s' http_requests=%s log_bytes=%ld\n",
            title[0] ? title : "(none)",
            count[0] ? count : "(none)",
            log_size);
    if (log_size >= 0 && log_size == last_log_size)
        stable_log_samples++;
    else {
        stable_log_samples = 0;
        excerpt_printed = 0;
    }
    last_log_size = log_size;

    if (!excerpt_printed && stable_log_samples >= 2 && log_size > 0) {
        int fd = open("/tmp/webkit_log.txt", O_RDONLY);
        if (fd >= 0) {
            char buf[2049];
            ssize_t n;
            off_t off = log_size > 2048 ? (off_t)log_size - 2048 : 0;

            lseek(fd, off, SEEK_SET);
            n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) {
                buf[n] = '\0';
                fprintf(stderr,
                        "[desktop] WebKit stalled log tail (%ld bytes):\n%s\n",
                        log_size, buf);
                excerpt_printed = 1;
            }
        }
    }
}

static pid_t launch_gst_registry_warmup(void)
{
    pid_t pid = fork();

    if (pid == 0) {
        char *argv[] = {
            "gst-inspect-1.0",
            "--gst-disable-registry-fork",
            "coreelements",
            "typefindfunctions",
            "playback",
            "isomp4",
            "matroska",
            "libav",
            "vpx",
            "opus",
            "ogg",
            "videoconvertscale",
            "audioconvert",
            NULL,
        };
        char *envp[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "LD_LIBRARY_PATH=/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_DIRS=/share:/usr/share",
            "GST_PLUGIN_SYSTEM_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_SCANNER=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_PLUGIN_SCANNER_1_0=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            NULL
        };
        int logfd;
        int nullfd;

        disable_child_coredumps();
        mkdir("/tmp/.cache", 0755);
        mkdir("/tmp/.cache/gstreamer-1.0", 0755);
        nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) {
            dup2(nullfd, 1);
            close(nullfd);
        }
        logfd = open("/tmp/gst-warmup.log",
                     O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logfd >= 0) {
            dup2(logfd, 2);
            close(logfd);
        }
        execve("/bin/gst-inspect-1.0", argv, envp);
        fprintf(stderr, "gst-inspect-1.0: execve failed errno=%d (%s)\n",
                errno, errno ? strerror(errno) : "no errno from kernel");
        _exit(127);
    }
    if (pid > 0)
        setpgid(pid, pid);
    return pid;
}

static void wait_for_gst_registry_warmup(int wait_us)
{
    const int step_us = 100000;
    int waited = 0;

    if (gst_warmup_pid <= 0)
        return;

    while (waited <= wait_us) {
        int status;
        pid_t exited = waitpid(gst_warmup_pid, &status, WNOHANG);

        if (exited == gst_warmup_pid) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                gst_registry_ready = 1;
                fprintf(stderr,
                        "[desktop] GStreamer registry warmup complete\n");
            } else {
                fprintf(stderr,
                        "[desktop] GStreamer registry warmup exited "
                        "(status %d)\n",
                        WIFEXITED(status) ? WEXITSTATUS(status) : status);
            }
            gst_warmup_pid = 0;
            return;
        }
        if (exited < 0 && errno == ECHILD) {
            gst_warmup_pid = 0;
            return;
        }
        if (waited >= wait_us)
            break;
        usleep(step_us);
        waited += step_us;
    }

    fprintf(stderr,
            "[desktop] GStreamer registry warmup still running after %d ms; "
            "continuing WebKit launch\n",
            wait_us / 1000);
}

static const char *client_path_basename(const char *path)
{
    const char *base = path ? path : "";

    for (const char *p = base; *p; p++) {
        if (*p == '/')
            base = p + 1;
    }
    return base;
}

static pid_t launch_client(const char *path, const char *name, const char *arg1,
                           const char *arg2, const char *arg3)
{
    const char *app_name = (name && name[0]) ? name :
        client_path_basename(path);
    int is_netsurf = strcmp(app_name, "netsurf") == 0;
    int is_minibrowser = strcmp(app_name, "MiniBrowser") == 0;
    int is_webkitgpusmoke = strcmp(app_name, "webkitgpusmoke") == 0;
    int is_mesa_gl = strcmp(app_name, "mesademo") == 0 ||
                     strcmp(app_name, "mesawlegl") == 0 ||
                     strcmp(app_name, "mesaglsmoke") == 0 ||
                     strcmp(app_name, "mesaeglinfo") == 0;
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

    if (is_webkit) {
        int requested_accel = webkit_accel_enabled_by_cmdline();
        int dmabuf_requested = is_minibrowser &&
            webkit_dmabuf_enabled_by_cmdline();
        int dmabuf_effective = dmabuf_requested;
        struct webkit_gpu_contract contract;
        int effective_accel = requested_accel;

        xv6_webkit_gpu_contract(&contract);
        if (dmabuf_effective &&
            !webkit_gpu_contract_allows_accel(&contract))
            dmabuf_effective = 0;
        if (effective_accel &&
            !webkit_gpu_contract_allows_accel(&contract) &&
            !dmabuf_effective)
            effective_accel = 0;
        if (dmabuf_effective)
            effective_accel = 1;

        write_webkit_gpu_policy_file(app_name, requested_accel, effective_accel,
                                     &contract,
                                     dmabuf_requested, dmabuf_effective);
    }

    pid_t pid = fork();
    if (pid == 0) {
        if (is_webkit)
            disable_child_coredumps();

        if (is_netsurf || is_webkit || is_mesa_gl) {
            const char *log_path = "/tmp/app_log.txt";
            int log_flags = O_WRONLY | O_CREAT | O_TRUNC;

            if (is_webkit) {
                if (webkit_logging_enabled_by_cmdline()) {
                    log_path = "/tmp/webkit_log.txt";
                } else {
                    log_path = "/dev/null";
                    log_flags = O_WRONLY;
                }
            }

            int logfd = open(log_path, log_flags, 0644);
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
            (char *)app_name,
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
        const char *minibrowser_url = arg1 ? arg1 : WEBKIT_DEFAULT_URL;
        int minibrowser_youtube_compat =
            is_minibrowser && webkit_youtube_compat_url(minibrowser_url);
        const char *minibrowser_feature_flags =
            webkit_request_idle_disabled_by_cmdline() ?
            webkit_feature_flags_no_idle : webkit_feature_flags;
        char *gst_registry_update_env = "GST_REGISTRY_UPDATE=yes";
        char *webkit_uri_log_env =
            webkit_logging_enabled_by_cmdline() ? "XV6_WEBKIT_URI_LOG=1" :
                                                  "XV6_WEBKIT_URI_LOG=0";
        char *webkit_gdk_gl_env =
            webkit_disable_gdk_gl_by_cmdline() ? "GDK_GL=disable" :
                                                 "GDK_GL=gles";
        char webkit_gst_disable_gl_sink_env[40];
        char webkit_gst_dmabuf_sink_disabled_env[44];
        char webkit_gst_use_videoconvert_env[48];
        char webkit_dmabuf_renderer_disable_gbm_env[48];
        char webkit_force_dmabuf_renderer_env[36];
        char mesa_wayland_color_buffers_env[48] =
            "XV6_MESA_WAYLAND_COLOR_BUFFERS=0";
        char mesa_perf_log_env[32] = "XV6_MESA_PERF_LOG=0";
        char mesa_wayland_throttle_env[40] = "XV6_MESA_WAYLAND_THROTTLE=1";

        snprintf(webkit_gst_disable_gl_sink_env,
                 sizeof(webkit_gst_disable_gl_sink_env),
                 "WEBKIT_GST_DISABLE_GL_SINK=%d",
                 webkit_gst_gl_enabled_by_cmdline() ? 0 : 1);
        snprintf(webkit_gst_dmabuf_sink_disabled_env,
                 sizeof(webkit_gst_dmabuf_sink_disabled_env),
                 "WEBKIT_GST_DMABUF_SINK_DISABLED=%d",
                 webkit_gst_dmabuf_sink_enabled_by_cmdline() ? 0 : 1);
        snprintf(webkit_gst_use_videoconvert_env,
                 sizeof(webkit_gst_use_videoconvert_env),
                 "WEBKIT_GST_USE_VIDEOCONVERT_SCALE=%d",
                 webkit_gst_gl_enabled_by_cmdline() ? 0 : 1);
        snprintf(webkit_dmabuf_renderer_disable_gbm_env,
                 sizeof(webkit_dmabuf_renderer_disable_gbm_env),
                 "WEBKIT_DMABUF_RENDERER_DISABLE_GBM=%d",
                 webkit_gbm_enabled_by_cmdline() ? 0 : 1);
        snprintf(webkit_force_dmabuf_renderer_env,
                 sizeof(webkit_force_dmabuf_renderer_env),
                 "WEBKIT_FORCE_DMABUF_RENDERER=%d",
                 webkit_dmabuf_enabled_by_cmdline() ? 1 : 0);
        if (is_mesa_gl) {
            char mesa_cmdline_buf[512];

            if (read_cmdline(mesa_cmdline_buf, sizeof(mesa_cmdline_buf)) == 0) {
                int mesa_color_buffers =
                    cmdline_int_value(mesa_cmdline_buf,
                                      "glsmoke_color_buffers", 0);

                if (mesa_color_buffers < 0)
                    mesa_color_buffers = 0;
                if (mesa_color_buffers > 4)
                    mesa_color_buffers = 4;
                snprintf(mesa_wayland_color_buffers_env,
                         sizeof(mesa_wayland_color_buffers_env),
                         "XV6_MESA_WAYLAND_COLOR_BUFFERS=%d",
                         mesa_color_buffers);
                snprintf(mesa_perf_log_env, sizeof(mesa_perf_log_env),
                         "XV6_MESA_PERF_LOG=%d",
                         cmdline_int_value(mesa_cmdline_buf,
                                           "glsmoke_mesa_perf", 0) != 0);
                snprintf(mesa_wayland_throttle_env,
                         sizeof(mesa_wayland_throttle_env),
                         "XV6_MESA_WAYLAND_THROTTLE=%d",
                         cmdline_int_value(mesa_cmdline_buf,
                                           "glsmoke_mesa_throttle", 1) != 0);
            }
        }
        char *argv_minibrowser[] = {
            (char *)app_name,
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
        char *argv_minibrowser_youtube[] = {
            (char *)app_name,
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
        char *argv_minibrowser_js[] = {
            (char *)app_name,
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
            (char *)app_name,
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
            (char *)app_name,
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
        char *argv_minibrowser_accel_youtube[] = {
            (char *)app_name,
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
        char *argv_minibrowser_accel_js[] = {
            (char *)app_name,
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
            (char *)app_name,
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
            (char *)app_name,
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
            (char *)app_name,
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
            "LD_PRELOAD=/lib/libpng16.so.16",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            webkit_gdk_gl_env,
            "GDK_DPI_SCALE=1.0",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "GIO_MODULE_DIR=/lib/gio/modules",
            "GIO_USE_TLS=gnutls",
            "GST_PLUGIN_SYSTEM_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_SCANNER=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_PLUGIN_SCANNER_1_0=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_GL_PLATFORM=egl",
            "GST_GL_WINDOW=wayland",
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            gst_registry_update_env,
            "XV6_GUI_SESSION=1",
            webkit_uri_log_env,
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            webkit_gst_disable_gl_sink_env,
            webkit_gst_dmabuf_sink_disabled_env,
            webkit_gst_use_videoconvert_env,
            "WEBKIT_GST_MAX_AVC1_RESOLUTION=720P",
            "WEBKIT_DISABLE_COMPOSITING_MODE=1",
            "WEBKIT_XV6_DISABLE_COMPOSITING_UPDATE=1",
            "LIBGL_ALWAYS_SOFTWARE=1",
            "EGL_PLATFORM=wayland",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "MESA_LOADER_DRIVER_OVERRIDE=swrast",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "WEBKIT_XV6_SKIP_RULE_FEATURES=1",
            "WEBKIT_XV6_SKIP_INITIAL_EMPTY_RENDER=1",
            "SOUP_FORCE_HTTP1=1",
            NULL
        };
        char *envp_minibrowser_accel[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "LD_LIBRARY_PATH=/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
            "LD_PRELOAD=/lib/libpng16.so.16",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_HOME=/tmp/.local/share",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            webkit_gdk_gl_env,
            "GDK_DPI_SCALE=1.0",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "GIO_MODULE_DIR=/lib/gio/modules",
            "GIO_USE_TLS=gnutls",
            "GST_PLUGIN_SYSTEM_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_SCANNER=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_PLUGIN_SCANNER_1_0=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_GL_PLATFORM=egl",
            "GST_GL_WINDOW=wayland",
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            gst_registry_update_env,
            "XV6_GUI_SESSION=1",
            webkit_uri_log_env,
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            webkit_dmabuf_renderer_disable_gbm_env,
            webkit_force_dmabuf_renderer_env,
            "WEBKIT_XV6_GPU_CONTRACT=virgl-opengl-submit",
            "WEBKIT_XV6_REQUIRE_GPU_CONTRACT=1",
            "WEBKIT_XV6_FORCE_COMPOSITING_MODE=1",
            "LIBGL_ALWAYS_SOFTWARE=0",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "GALLIUM_DRIVER=virgl",
            "EGL_PLATFORM=wayland",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "SOUP_FORCE_HTTP1=1",
            NULL
        };
        char *envp_minibrowser_dxg[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "LD_LIBRARY_PATH=/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
            "LD_PRELOAD=/lib/libpng16.so.16",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_HOME=/tmp/.local/share",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            webkit_gdk_gl_env,
            "GDK_DPI_SCALE=1.0",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "GIO_MODULE_DIR=/lib/gio/modules",
            "GIO_USE_TLS=gnutls",
            "GST_PLUGIN_SYSTEM_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_SCANNER=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_PLUGIN_SCANNER_1_0=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_GL_PLATFORM=egl",
            "GST_GL_WINDOW=wayland",
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            gst_registry_update_env,
            "XV6_GUI_SESSION=1",
            webkit_uri_log_env,
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            webkit_gst_dmabuf_sink_disabled_env,
            webkit_force_dmabuf_renderer_env,
            "WEBKIT_XV6_GPU_CONTRACT=d3d12-shared-surface",
            "WEBKIT_XV6_REQUIRE_GPU_CONTRACT=1",
            "WEBKIT_XV6_FORCE_COMPOSITING_MODE=1",
            "LIBGL_ALWAYS_SOFTWARE=0",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "MESA_LOADER_DRIVER_OVERRIDE=d3d12",
            "GALLIUM_DRIVER=d3d12",
            "EGL_PLATFORM=wayland",
            "XV6_MESA_WAYLAND_THROTTLE=0",
            "XV6_MESA_WAYLAND_XV6GPU=1",
            "XV6_MESA_PERF_LOG=0",
            "XV6_MESA_WAYLAND_INPLACE_PRESENT=1",
            "XV6_D3D12_ENABLE_NATIVE_PRESENT=1",
            "XV6_D3D12_PRESENT_INTERVAL=1",
            "XV6_D3D12_REQUIRE_NATIVE_PRESENT=1",
            "XV6_D3D12_COPY_EXPORT=0",
            "vblank_mode=0",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "SOUP_FORCE_HTTP1=1",
            NULL
        };
        char *envp_minibrowser_accel_sw[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "LD_LIBRARY_PATH=/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
            "LD_PRELOAD=/lib/libpng16.so.16",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_HOME=/tmp/.local/share",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            webkit_gdk_gl_env,
            "GDK_DPI_SCALE=1.0",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "GIO_MODULE_DIR=/lib/gio/modules",
            "GIO_USE_TLS=gnutls",
            "GST_PLUGIN_SYSTEM_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_SCANNER=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_PLUGIN_SCANNER_1_0=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_GL_PLATFORM=egl",
            "GST_GL_WINDOW=wayland",
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            gst_registry_update_env,
            "XV6_GUI_SESSION=1",
            webkit_uri_log_env,
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            webkit_gst_disable_gl_sink_env,
            webkit_gst_dmabuf_sink_disabled_env,
            webkit_gst_use_videoconvert_env,
            "WEBKIT_GST_MAX_AVC1_RESOLUTION=720P",
            "LIBGL_ALWAYS_SOFTWARE=1",
            "EGL_PLATFORM=wayland",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "MESA_LOADER_DRIVER_OVERRIDE=swrast",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "WEBKIT_XV6_DISABLE_BCG_SWITCH=1",
            "WEBKIT_XV6_SKIP_RULE_FEATURES=1",
            "SOUP_FORCE_HTTP1=1",
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
            mesa_wayland_throttle_env,
            mesa_perf_log_env,
            "XV6_MESAWLEGL_SHM_PRESENT=0",
            mesa_wayland_color_buffers_env,
            NULL
        };
        char *envp_mesa_dxg[] = {
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
            "MESA_LOADER_DRIVER_OVERRIDE=d3d12",
            "GALLIUM_DRIVER=d3d12",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "EGL_PLATFORM=wayland",
            mesa_wayland_throttle_env,
            "XV6_MESA_WAYLAND_XV6GPU=1",
            mesa_perf_log_env,
            "XV6_MESA_WAYLAND_INPLACE_PRESENT=1",
            "XV6_D3D12_ENABLE_NATIVE_PRESENT=1",
            "XV6_D3D12_PRESENT_INTERVAL=1",
            "XV6_D3D12_REQUIRE_NATIVE_PRESENT=1",
            "XV6_D3D12_COPY_EXPORT=0",
            "vblank_mode=0",
            mesa_wayland_color_buffers_env,
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
            "MESA_LOADER_DRIVER_OVERRIDE=swrast",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            mesa_wayland_color_buffers_env,
            NULL
        };
        int minibrowser_accel =
            is_minibrowser && webkit_accel_enabled_by_cmdline();
        int minibrowser_dmabuf =
            is_minibrowser && webkit_dmabuf_enabled_by_cmdline();
        int minibrowser_js =
            is_minibrowser && !webkit_js_disabled_by_cmdline();
        int minibrowser_webgl_smoke =
            is_minibrowser && webkit_webgl_smoke_enabled_by_cmdline();
        int webkit_accel =
            (is_minibrowser && minibrowser_accel) ||
            (is_webkitgpusmoke && webkit_accel_enabled_by_cmdline());
        struct webkit_gpu_contract contract;
        char **argv_exec = argv_default;
        char **envp_webkit = envp_minibrowser;
        xv6_webkit_gpu_contract(&contract);
        if (minibrowser_dmabuf && contract.dxg_transport) {
            fprintf(stderr,
                    "[desktop] WebKit dmabuf requested on Hyper-V, but the "
                    "only accelerated contract is D3D12 shared-resource "
                    "OpenGL-submit; using the stable WebKit compositor path\n");
            minibrowser_dmabuf = 0;
        }
        if (minibrowser_dmabuf && !contract.render_node) {
            fprintf(stderr,
                    "[desktop] WebKit dmabuf requested, but no render node "
                    "is available; using the stable WebKit compositor path\n");
            minibrowser_dmabuf = 0;
        }
        if (minibrowser_dmabuf && !contract.opengl_submit) {
            fprintf(stderr,
                    "[desktop] WebKit dmabuf requested, but OpenGL submit is "
                    "not available; using the stable WebKit compositor path\n");
            minibrowser_dmabuf = 0;
        }
        if (minibrowser_dmabuf && !contract.shared_surface) {
            fprintf(stderr,
                    "[desktop] WebKit dmabuf requested, but the shared-surface "
                    "present contract is not available; using the stable "
                    "WebKit compositor path\n");
            minibrowser_dmabuf = 0;
        }
        if (webkit_accel &&
            !webkit_gpu_contract_allows_accel(&contract) &&
            !minibrowser_dmabuf) {
            if (contract.dxg_transport) {
                fprintf(stderr,
                        "[desktop] Hyper-V DXG GPU-PV transport is open, "
                        "but the D3D12 shared-surface/OpenGL-submit contract "
                        "is not available; "
                        "using the stable WebKit compositor path\n");
            } else {
                fprintf(stderr,
                        "[desktop] WebKit acceleration requested, but virgl "
                        "is unavailable; using the stable WebKit compositor "
                        "path\n");
            }
            minibrowser_accel = 0;
            webkit_accel = 0;
        }
        if (minibrowser_dmabuf)
            webkit_accel = 1;
        if (is_webkit) {
            write_webkit_gpu_policy_file(app_name,
                                         webkit_accel_enabled_by_cmdline(),
                                         webkit_accel,
                                         &contract,
                                         is_minibrowser &&
                                             webkit_dmabuf_enabled_by_cmdline(),
                                         minibrowser_dmabuf);
        }
        if (is_webkit && webkit_accel) {
            if (contract.d3d12_present)
                envp_webkit = envp_minibrowser_dxg;
            else if (contract.virgl_opengl)
                envp_webkit = envp_minibrowser_accel;
            else
                envp_webkit = envp_minibrowser_accel_sw;
        }
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
            fprintf(stderr,
                    "[desktop] MiniBrowser argv js=%d accel=%d dmabuf=%d "
                    "webgl=%d youtube_compat=%d arg4=%s url=%s\n",
                    minibrowser_js, minibrowser_accel, minibrowser_dmabuf,
                    minibrowser_webgl_smoke, minibrowser_youtube_compat,
                    argv_exec[4] ? argv_exec[4] : "(none)",
                    minibrowser_url);
        }
        errno = 0;
        execve(path,
               argv_exec,
               is_webkit ? envp_webkit :
                    (is_mesa_gl ?
                    (xv6_opengl_submit_available() ? envp_mesa_accel :
                     (xv6_dxg_transport_available() &&
                      xv6_render_node_available() ?
                          envp_mesa_dxg : envp_mesa_accel_sw)) :
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
