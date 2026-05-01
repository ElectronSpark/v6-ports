#!/usr/bin/env bash
# Apply the xv6 WebKitGTK source porting files captured in this repo.
#
# Usage:
#   ports/webkit/apply-xv6-overrides.sh /path/to/webkitgtk-2.42.5
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <webkitgtk-2.42.5-source-dir>" >&2
    exit 2
fi

src="$1"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
overrides="${script_dir}/overrides/webkitgtk-2.42.5"

if [[ ! -d "${src}" || ! -f "${src}/CMakeLists.txt" ]]; then
    echo "ports/webkit: ${src} is not a WebKitGTK source tree" >&2
    exit 1
fi
if [[ ! -d "${overrides}" ]]; then
    echo "ports/webkit: no xv6 WebKitGTK source overrides to apply"
    exit 0
fi

mapfile -t override_files < <(cd "${overrides}" && find . -type f | sed 's#^\./##' | sort)
if [[ ${#override_files[@]} -eq 0 ]]; then
    echo "ports/webkit: no xv6 WebKitGTK source overrides to apply"
    exit 0
fi

for rel in "${override_files[@]}"; do
    mkdir -p "${src}/$(dirname "${rel}")"
    cp -p "${overrides}/${rel}" "${src}/${rel}"
done

minibrowser_main="${src}/Tools/MiniBrowser/gtk/main.c"
if [[ -f "${minibrowser_main}" ]]; then
    if grep -q '"process-swap-on-cross-site-navigation-enabled", TRUE' \
            "${minibrowser_main}"; then
        perl -0pi -e \
            's/"process-swap-on-cross-site-navigation-enabled", TRUE/"process-swap-on-cross-site-navigation-enabled", FALSE/g' \
            "${minibrowser_main}"
    elif ! grep -q '"process-swap-on-cross-site-navigation-enabled", FALSE' \
            "${minibrowser_main}"; then
        echo "ports/webkit: MiniBrowser process-swap context block not found" >&2
        exit 1
    fi
    if ! grep -Fq 'webkit_settings_set_hardware_acceleration_policy(webkitSettings, WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS);' \
            "${minibrowser_main}"; then
        if grep -Fq 'webkit_settings_set_enable_webgl(webkitSettings, TRUE);' \
                "${minibrowser_main}"; then
            perl -0pi -e \
                's/(webkit_settings_set_enable_webgl\(webkitSettings, TRUE\);\n)/$1    webkit_settings_set_hardware_acceleration_policy(webkitSettings, WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS);\n/' \
                "${minibrowser_main}"
        else
            echo "ports/webkit: MiniBrowser WebGL settings block not found" >&2
            exit 1
        fi
    fi
fi

minibrowser_tab="${src}/Tools/MiniBrowser/gtk/BrowserTab.c"
if [[ -f "${minibrowser_tab}" ]]; then
    old='if (!uri || !g_str_has_prefix(uri, "https://www.google."))'
    if grep -Fq "${old}" "${minibrowser_tab}"; then
        perl -0pi -e \
            's/if \(!uri \|\| !g_str_has_prefix\(uri, "https:\/\/www\.google\."\)\)/if (!uri || (!g_str_has_prefix(uri, "https:\/\/www.google.") \&\&\n        !g_str_has_prefix(uri, "https:\/\/www.youtube.")))/g' \
            "${minibrowser_tab}"
    elif ! grep -Fq 'https://www.youtube.' "${minibrowser_tab}"; then
        echo "ports/webkit: MiniBrowser probe URL block not found" >&2
        exit 1
    fi
    if ! grep -Fq 'installXV6MediaDomShim' "${minibrowser_tab}"; then
        perl -0pi -e 's#(static gchar \*getWebViewOrigin\(WebKitWebView \*webView\)\n\{\n    WebKitSecurityOrigin \*origin = webkit_security_origin_new_for_uri\(webkit_web_view_get_uri\(webView\)\);\n    gchar \*originStr = webkit_security_origin_to_string\(origin\);\n    webkit_security_origin_unref\(origin\);\n\n    return originStr;\n\}\n)#$1\nstatic void installXV6MediaDomShim(WebKitWebView *webView)\n{\n    static const char *script =\n        "(function(){"\n        "function install(name,parentName){"\n        "if(typeof window[name]!==\\"undefined\\")return;"\n        "var parent=parentName&&window[parentName]&&window[parentName].prototype;"\n        "function Ctor(){}"\n        "try{Object.defineProperty(Ctor,\\"name\\",{value:name});}catch(e){}"\n        "Ctor.prototype=parent?Object.create(parent):{};"\n        "try{Object.defineProperty(Ctor.prototype,\\"constructor\\",{value:Ctor});}catch(e){Ctor.prototype.constructor=Ctor;}"\n        "try{Object.defineProperty(window,name,{value:Ctor,configurable:true,writable:true});}catch(e){window[name]=Ctor;}"\n        "}"\n        "install(\\"HTMLMediaElement\\",\\"HTMLElement\\");"\n        "install(\\"HTMLVideoElement\\",\\"HTMLMediaElement\\");"\n        "install(\\"HTMLAudioElement\\",\\"HTMLMediaElement\\");"\n        "var p=window.HTMLMediaElement&&window.HTMLMediaElement.prototype;"\n        "if(p){"\n        "if(!p.load)p.load=function(){};"\n        "if(!p.pause)p.pause=function(){};"\n        "if(!p.play)p.play=function(){return Promise.resolve();};"\n        "if(!p.canPlayType)p.canPlayType=function(){return \\"maybe\\";};"\n        "}"\n        "})();";\n    WebKitUserContentManager *manager = webkit_web_view_get_user_content_manager(webView);\n    WebKitUserScript *userScript = webkit_user_script_new(\n        script,\n        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,\n        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,\n        NULL,\n        NULL);\n    webkit_user_content_manager_add_script(manager, userScript);\n    webkit_user_script_unref(userScript);\n}\n#' \
            "${minibrowser_tab}"
        if ! grep -Fq 'installXV6MediaDomShim' "${minibrowser_tab}"; then
            echo "ports/webkit: MiniBrowser origin helper block not found" >&2
            exit 1
        fi
        perl -0pi -e 's/(G_OBJECT_CLASS\(browser_tab_parent_class\)->constructed\(gObject\);\n)/$1    installXV6MediaDomShim(tab->webView);\n/' \
            "${minibrowser_tab}"
    fi
    if ! grep -Fq 'typeof window.Audio' "${minibrowser_tab}"; then
        if grep -Fq '"install(\"HTMLAudioElement\",\"HTMLMediaElement\");"' "${minibrowser_tab}"; then
            perl -0pi -e \
        's#("install\(\\"HTMLAudioElement\\",\\"HTMLMediaElement\\"\);"\n)#$1        "if(typeof window.Audio===\\"undefined\\")window.Audio=function(){var a=document.createElement(\\"audio\\");if(!a.load)a.load=function(){};if(!a.pause)a.pause=function(){};if(!a.play)a.play=function(){return Promise.resolve();};if(!a.canPlayType)a.canPlayType=function(){return \\"maybe\\";};return a;};"\n        "var cacheObj={match:function(){return Promise.resolve(void 0);},put:function(){return Promise.resolve();},add:function(){return Promise.resolve();},addAll:function(){return Promise.resolve();},delete:function(){return Promise.resolve(false);},keys:function(){return Promise.resolve([]);}};"\n        "var cachesObj={open:function(){return Promise.resolve(cacheObj);},match:function(){return Promise.resolve(void 0);},has:function(){return Promise.resolve(false);},delete:function(){return Promise.resolve(false);},keys:function(){return Promise.resolve([]);}};"\n        "try{Object.defineProperty(window,\\"caches\\",{value:cachesObj,configurable:true});}catch(e){window.caches=cachesObj;}"\n        "if(window.navigator){var sw={register:function(){return Promise.resolve(void 0);},getRegistration:function(){return Promise.resolve(void 0);},getRegistrations:function(){return Promise.resolve([]);},ready:Promise.resolve(void 0)};try{Object.defineProperty(window.navigator,\\"serviceWorker\\",{value:sw,configurable:true});}catch(e){window.navigator.serviceWorker=sw;}}"\n#' \
                "${minibrowser_tab}"
        else
            echo "ports/webkit: MiniBrowser media DOM shim audio anchor not found" >&2
            exit 1
        fi
    fi
    if ! grep -Fq 'var cacheObj=' "${minibrowser_tab}"; then
        if grep -Fq '"if(typeof window.Audio===' "${minibrowser_tab}"; then
            perl -0pi -e \
        's#("if\(typeof window.Audio===\\"undefined\\"\).*?;"\n)#$1        "var cacheObj={match:function(){return Promise.resolve(void 0);},put:function(){return Promise.resolve();},add:function(){return Promise.resolve();},addAll:function(){return Promise.resolve();},delete:function(){return Promise.resolve(false);},keys:function(){return Promise.resolve([]);}};"\n        "var cachesObj={open:function(){return Promise.resolve(cacheObj);},match:function(){return Promise.resolve(void 0);},has:function(){return Promise.resolve(false);},delete:function(){return Promise.resolve(false);},keys:function(){return Promise.resolve([]);}};"\n        "try{Object.defineProperty(window,\\"caches\\",{value:cachesObj,configurable:true});}catch(e){window.caches=cachesObj;}"\n        "if(window.navigator){var sw={register:function(){return Promise.resolve(void 0);},getRegistration:function(){return Promise.resolve(void 0);},getRegistrations:function(){return Promise.resolve([]);},ready:Promise.resolve(void 0)};try{Object.defineProperty(window.navigator,\\"serviceWorker\\",{value:sw,configurable:true});}catch(e){window.navigator.serviceWorker=sw;}}"\n#s' \
                "${minibrowser_tab}"
        else
            echo "ports/webkit: MiniBrowser cache shim anchor not found" >&2
            exit 1
        fi
    fi
    if ! grep -Fq 'requestIdleCallback' "${minibrowser_tab}"; then
        perl -0pi -e \
            's#("if\(window\.navigator\)\{var sw=.*?window\.navigator\.serviceWorker=sw;\}\}"\n)#$1        "if(typeof window.requestIdleCallback===\\"undefined\\")window.requestIdleCallback=function(cb){return setTimeout(function(){cb({didTimeout:false,timeRemaining:function(){return 50;}});},1);};"\n        "if(typeof window.cancelIdleCallback===\\"undefined\\")window.cancelIdleCallback=function(id){clearTimeout(id);};"\n        "if(window.HTMLImageElement&&HTMLImageElement.prototype&&!HTMLImageElement.prototype.decode)HTMLImageElement.prototype.decode=function(){return Promise.resolve();};"\n#s' \
            "${minibrowser_tab}"
    fi
    if ! grep -Fq 'nativeTree:{' "${minibrowser_tab}"; then
        perl -0pi -e \
            's#("if\(window\.HTMLImageElement&&HTMLImageElement\.prototype&&!HTMLImageElement\.prototype\.decode\).*?;"\n)#$1        "if(!window.ShadyDOM)window.ShadyDOM={};"\n        "if(!window.ShadyDOM.patch)window.ShadyDOM.patch=function(n){return n;};"\n        "if(!window.ShadyDOM.wrap)window.ShadyDOM.wrap=function(n){return n;};"\n        "if(!window.ShadyDOM.wrapIfNeeded)window.ShadyDOM.wrapIfNeeded=function(n){return n;};"\n        "if(!window.ShadyDOM.Wrapper){function XV6ShadyWrapper(n){return n||this;}XV6ShadyWrapper.prototype=Object.create(Element.prototype);try{Object.defineProperty(XV6ShadyWrapper.prototype,\\"constructor\\",{value:XV6ShadyWrapper});}catch(e){XV6ShadyWrapper.prototype.constructor=XV6ShadyWrapper;}window.ShadyDOM.Wrapper=XV6ShadyWrapper;}"\n        "if(!window.ShadyDOM.flush)window.ShadyDOM.flush=function(){};"\n        "if(!window.ShadyDOM.enqueue)window.ShadyDOM.enqueue=function(fn){return setTimeout(fn,0);};"\n        "if(!window.ShadyDOM.nativeMethods)window.ShadyDOM.nativeMethods={querySelectorAll:function(s){return this.querySelectorAll(s);},querySelector:function(s){return this.querySelector(s);},setAttribute:function(n,v){return this.setAttribute(n,v);},removeAttribute:function(n){return this.removeAttribute(n);},appendChild:function(n){return this.appendChild(n);},insertBefore:function(n,r){return this.insertBefore(n,r);},removeChild:function(n){return this.removeChild(n);}};"\n        "if(!window.ShadyDOM.nativeTree)window.ShadyDOM.nativeTree={querySelectorAll:window.ShadyDOM.nativeMethods.querySelectorAll,querySelector:window.ShadyDOM.nativeMethods.querySelector};"\n        "if(!window.ShadyCSS)window.ShadyCSS={nativeShadow:true,nativeCss:true,cssBuild:\\"\\",disableRuntime:false,prepareTemplate:function(){},prepareTemplateDom:function(){},prepareTemplateStyles:function(){},styleSubtree:function(){},styleElement:function(){},styleDocument:function(){},flushCustomStyles:function(){},getComputedStyleValue:function(e,p){return getComputedStyle(e).getPropertyValue(p);}};"\n#s' \
            "${minibrowser_tab}"
    fi
    if ! grep -Fq 'XV6-ERRSTACK' "${minibrowser_tab}"; then
        python3 - "${minibrowser_tab}" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
anchor = '        "(function(){"\n'
injection = (
    '        "function xv6Stack(e){try{return e&&(e.stack||e.message||String(e));}catch(x){return String(e);}}"\n'
    '        "window.addEventListener(\'error\',function(e){try{console.error(\'XV6-ERRSTACK \'+(e.message||\'error\')+\' @ \'+(e.filename||\'\')+\':\'+(e.lineno||0)+\':\'+(e.colno||0)+\' stack=\'+(xv6Stack(e.error)||\'\'));}catch(x){}},true);"\n'
    '        "window.addEventListener(\'unhandledrejection\',function(e){try{console.error(\'XV6-REJSTACK \'+(xv6Stack(e.reason)||\'\'));}catch(x){}},true);"\n'
)
if anchor not in text:
    raise SystemExit("ports/webkit: MiniBrowser diagnostic shim anchor not found")
text = text.replace(anchor, anchor + injection, 1)
path.write_text(text)
PY
    fi
fi

resource_request_soup="${src}/Source/WebCore/platform/network/soup/ResourceRequestSoup.cpp"
if [[ -f "${resource_request_soup}" ]]; then
    python3 - "${resource_request_soup}" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()

if "xv6ApplyYouTubeHeaders" not in text:
    if "#include <cstring>\n" not in text:
        text = text.replace("#include \"WebKitFormDataInputStream.h\"\n", "#include \"WebKitFormDataInputStream.h\"\n#include <cstring>\n", 1)

    anchor = "namespace WebCore {\n\n"
    helper = r'''namespace WebCore {

static bool xv6IsYouTubeHost(const CString& host)
{
    return !strcmp(host.data(), "youtube.com")
        || !strcmp(host.data(), "www.youtube.com")
        || !strcmp(host.data(), "m.youtube.com");
}

static void xv6ApplyYouTubeHeaders(const URL& url, SoupMessageHeaders* requestHeaders)
{
    auto host = url.host().toString().utf8();
    if (!xv6IsYouTubeHost(host))
        return;

    if (!strcmp(host.data(), "m.youtube.com")) {
        soup_message_headers_replace(requestHeaders, "User-Agent",
            "Mozilla/5.0 (Linux; Android 6.0; Nexus 5 Build/MRA58N) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/49.0.2623.105 Mobile Safari/537.36");
        soup_message_headers_replace(requestHeaders, "Sec-CH-UA", "\"Chromium\";v=\"49\", \"Google Chrome\";v=\"49\", \"Not-A.Brand\";v=\"99\"");
        soup_message_headers_replace(requestHeaders, "Sec-CH-UA-Mobile", "?1");
        soup_message_headers_replace(requestHeaders, "Sec-CH-UA-Platform", "\"Android\"");
    } else {
        soup_message_headers_replace(requestHeaders, "User-Agent",
            "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36");
        soup_message_headers_replace(requestHeaders, "Sec-CH-UA", "\"Chromium\";v=\"124\", \"Google Chrome\";v=\"124\", \"Not-A.Brand\";v=\"99\"");
        soup_message_headers_replace(requestHeaders, "Sec-CH-UA-Mobile", "?0");
        soup_message_headers_replace(requestHeaders, "Sec-CH-UA-Platform", "\"Linux\"");
    }
    soup_message_headers_replace(requestHeaders, "Accept-Language", "en-US,en;q=0.9");
    soup_message_headers_replace(requestHeaders, "Cache-Control", "no-cache");
    soup_message_headers_replace(requestHeaders, "Pragma", "no-cache");
}

'''
    if anchor not in text:
        raise SystemExit("ports/webkit: ResourceRequestSoup namespace anchor not found")
    text = text.replace(anchor, helper, 1)

text = text.replace("xv6ApplyYouTubeMobileHeaders(url(), requestHeaders);", "xv6ApplyYouTubeHeaders(url(), requestHeaders);")
if 'soup_message_headers_replace(requestHeaders, "Cache-Control", "no-cache");' not in text:
    text = text.replace(
        '    soup_message_headers_replace(requestHeaders, "Accept-Language", "en-US,en;q=0.9");\n',
        '    soup_message_headers_replace(requestHeaders, "Accept-Language", "en-US,en;q=0.9");\n'
        '    soup_message_headers_replace(requestHeaders, "Cache-Control", "no-cache");\n'
        '    soup_message_headers_replace(requestHeaders, "Pragma", "no-cache");\n',
        1)

if "xv6ApplyYouTubeHeaders(url(), requestHeaders);" not in text:
    needle = '    soup_message_headers_replace(requestHeaders, "Accept-Encoding", "identity");\n'
    if needle not in text:
        raise SystemExit("ports/webkit: ResourceRequestSoup Accept-Encoding anchor not found")
    text = text.replace(needle, needle + "    xv6ApplyYouTubeHeaders(url(), requestHeaders);\n", 1)

path.write_text(text)
PY
    if ! grep -Fq 'soup_message_set_force_http1(soupMessage.get(), true);' "${resource_request_soup}"; then
        if grep -Fq 'soup_message_set_priority(soupMessage.get(), toSoupMessagePriority(priority()));' "${resource_request_soup}"; then
            perl -0pi -e \
                's/(soup_message_set_priority\(soupMessage\.get\(\), toSoupMessagePriority\(priority\(\)\)\);)/$1\n    soup_message_set_force_http1(soupMessage.get(), true);/g' \
                "${resource_request_soup}"
        else
            echo "ports/webkit: ResourceRequestSoup priority block not found" >&2
            exit 1
        fi
    fi
    if ! grep -Fq 'soup_message_headers_replace(requestHeaders, "Accept-Encoding", "identity");' "${resource_request_soup}"; then
        if grep -Fq 'updateSoupMessageHeaders(soup_message_get_request_headers(soupMessage.get()));' "${resource_request_soup}"; then
            perl -0pi -e \
                's/updateSoupMessageHeaders\(soup_message_get_request_headers\(soupMessage\.get\(\)\)\);/auto* requestHeaders = soup_message_get_request_headers(soupMessage.get());\n    updateSoupMessageHeaders(requestHeaders);\n    soup_message_headers_replace(requestHeaders, "Accept-Encoding", "identity");/g' \
                "${resource_request_soup}"
        else
            echo "ports/webkit: ResourceRequestSoup request-header block not found" >&2
            exit 1
        fi
    fi
    if grep -Fq 'if (!acceptEncoding())' "${resource_request_soup}"; then
        perl -0pi -e \
            's/if \(!acceptEncoding\(\)\)\n        soup_message_disable_feature\(soupMessage\.get\(\), SOUP_TYPE_CONTENT_DECODER\);/soup_message_disable_feature(soupMessage.get(), SOUP_TYPE_CONTENT_DECODER);/g' \
            "${resource_request_soup}"
    fi
    if ! grep -Fq 'soup_message_disable_feature(soupMessage.get(), SOUP_TYPE_CONTENT_DECODER);' "${resource_request_soup}"; then
        echo "ports/webkit: ResourceRequestSoup content-decoder block not found" >&2
        exit 1
    fi
    if grep -Fq 'youtube.com"_s' "${resource_request_soup}"; then
        echo "ports/webkit: stale YouTube content-decoder override remains" >&2
        exit 1
    fi
fi

process_launcher_glib="${src}/Source/WebKit/UIProcess/Launcher/glib/ProcessLauncherGLib.cpp"
if [[ -f "${process_launcher_glib}" ]]; then
    marker='g_subprocess_launcher_setenv(launcher.get(), "WAYLAND_DISPLAY", "wayland-0", TRUE);'
    anchor='g_subprocess_launcher_take_fd(launcher.get(), socketPair.client, socketPair.client);'
    if ! grep -Fq "${marker}" "${process_launcher_glib}"; then
        if grep -Fq "${anchor}" "${process_launcher_glib}"; then
            perl -0pi -e 's#g_subprocess_launcher_take_fd\(launcher\.get\(\), socketPair\.client, socketPair\.client\);#g_subprocess_launcher_take_fd(launcher.get(), socketPair.client, socketPair.client);\n    if (g_getenv("XV6_GUI_SESSION")) {\n        g_subprocess_launcher_setenv(launcher.get(), "HOME", "/", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "PATH", "/bin:/usr/bin", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "XDG_RUNTIME_DIR", "/tmp", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "XDG_CACHE_HOME", "/tmp/.cache", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "XDG_DATA_HOME", "/tmp/.local/share", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "XDG_DATA_DIRS", "/share:/usr/share", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "WAYLAND_DISPLAY", "wayland-0", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "GDK_BACKEND", "wayland", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "GDK_DPI_SCALE", "1.55", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "GIO_MODULE_DIR", "/lib/gio/modules", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "GIO_USE_TLS", "openssl", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "SSL_CERT_FILE", "/share/netsurf/ca-bundle", TRUE);\n        const char* libglAlwaysSoftware = g_getenv("LIBGL_ALWAYS_SOFTWARE");\n        const char* mesaDriver = g_getenv("MESA_LOADER_DRIVER_OVERRIDE");\n        const char* galliumDriver = g_getenv("GALLIUM_DRIVER");\n        const char* epoxyAllowMissing = g_getenv("EPOXY_XV6_ALLOW_MISSING");\n        const char* soupForceHttp1 = g_getenv("SOUP_FORCE_HTTP1");\n        const char* xv6DisableBCGSwitch = g_getenv("WEBKIT_XV6_DISABLE_BCG_SWITCH");\n        g_subprocess_launcher_setenv(launcher.get(), "LIBGL_ALWAYS_SOFTWARE", libglAlwaysSoftware ? libglAlwaysSoftware : "0", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "MESA_LOADER_DRIVER_OVERRIDE", mesaDriver ? mesaDriver : "virpipe", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "GALLIUM_DRIVER", galliumDriver ? galliumDriver : "virpipe", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "EPOXY_XV6_ALLOW_MISSING", epoxyAllowMissing ? epoxyAllowMissing : "1", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "SOUP_FORCE_HTTP1", soupForceHttp1 ? soupForceHttp1 : "1", TRUE);\n        g_subprocess_launcher_setenv(launcher.get(), "WEBKIT_XV6_DISABLE_BCG_SWITCH", xv6DisableBCGSwitch ? xv6DisableBCGSwitch : "1", TRUE);\n    }#' \
                "${process_launcher_glib}"
        else
            echo "ports/webkit: ProcessLauncherGLib launcher fd anchor not found" >&2
            exit 1
        fi
    fi
    if ! grep -Fq 'WEBKIT_XV6_DISABLE_BCG_SWITCH' "${process_launcher_glib}"; then
        perl -0pi -e \
            's/(const char\* soupForceHttp1 = g_getenv\("SOUP_FORCE_HTTP1"\);\n)/$1        const char* xv6DisableBCGSwitch = g_getenv("WEBKIT_XV6_DISABLE_BCG_SWITCH");\n/g; s/(g_subprocess_launcher_setenv\(launcher\.get\(\), "SOUP_FORCE_HTTP1", soupForceHttp1 \? soupForceHttp1 : "1", TRUE\);\n)/$1        g_subprocess_launcher_setenv(launcher.get(), "WEBKIT_XV6_DISABLE_BCG_SWITCH", xv6DisableBCGSwitch ? xv6DisableBCGSwitch : "1", TRUE);\n/g' \
            "${process_launcher_glib}"
    fi
fi

network_process_proxy="${src}/Source/WebKit/UIProcess/Network/NetworkProcessProxy.cpp"
if [[ -f "${network_process_proxy}" ]]; then
    if ! grep -Fq 'WEBKIT_XV6_DISABLE_BCG_SWITCH' "${network_process_proxy}"; then
        if grep -Fq '#include <cstdio>' "${network_process_proxy}"; then
            perl -0pi -e 's/#include <cstdio>/#include <cstdio>\n#include <cstdlib>/g' \
                "${network_process_proxy}"
        fi
        if grep -Fq 'void NetworkProcessProxy::triggerBrowsingContextGroupSwitchForNavigation(WebPageProxyIdentifier pageID, uint64_t navigationID, BrowsingContextGroupSwitchDecision browsingContextGroupSwitchDecision, const WebCore::RegistrableDomain& responseDomain, NetworkResourceLoadIdentifier existingNetworkResourceLoadIdentifierToResume, CompletionHandler<void(bool success)>&& completionHandler)' "${network_process_proxy}"; then
            perl -0pi -e 's#(void NetworkProcessProxy::triggerBrowsingContextGroupSwitchForNavigation\(WebPageProxyIdentifier pageID, uint64_t navigationID, BrowsingContextGroupSwitchDecision browsingContextGroupSwitchDecision, const WebCore::RegistrableDomain& responseDomain, NetworkResourceLoadIdentifier existingNetworkResourceLoadIdentifierToResume, CompletionHandler<void\(bool success\)>&& completionHandler\)\n\{)#$1\n    if (getenv("WEBKIT_XV6_DISABLE_BCG_SWITCH")) {\n        fprintf(stderr, "[NP-BCG] suppressed pageID=%" PRIu64 " navigationID=%" PRIu64 " decision=%u existing=%" PRIu64 "\\n",\n            pageID.toUInt64(), navigationID, static_cast<unsigned>(browsingContextGroupSwitchDecision), existingNetworkResourceLoadIdentifierToResume.toUInt64());\n        completionHandler(false);\n        return;\n    }\n#g' \
                "${network_process_proxy}"
        else
            echo "ports/webkit: NetworkProcessProxy BCG switch hook not found" >&2
            exit 1
        fi
    fi
fi

network_resource_loader="${src}/Source/WebKit/NetworkProcess/NetworkResourceLoader.cpp"
if [[ -f "${network_resource_loader}" ]]; then
    if ! grep -Fq '[NRL-BCG] staying-in-group' "${network_resource_loader}"; then
        if grep -Fq '#include <cstdio>' "${network_resource_loader}"; then
            perl -0pi -e 's/#include <cstdio>/#include <cstdio>\n#include <cstdlib>/g' \
                "${network_resource_loader}"
        fi
        if grep -Fq 'if (browsingContextGroupSwitchDecision == BrowsingContextGroupSwitchDecision::StayInGroup) {' "${network_resource_loader}"; then
            perl -0pi -e 's#if \(browsingContextGroupSwitchDecision == BrowsingContextGroupSwitchDecision::StayInGroup\) \{#if (getenv("WEBKIT_XV6_DISABLE_BCG_SWITCH") \&\& browsingContextGroupSwitchDecision != BrowsingContextGroupSwitchDecision::StayInGroup)\\n        fprintf(stderr, "[NRL-BCG] staying-in-group url=%s decision=%u\\\\n",\\n            response.url().string().utf8().data(), static_cast<unsigned>(browsingContextGroupSwitchDecision));\\n\\n    if (browsingContextGroupSwitchDecision == BrowsingContextGroupSwitchDecision::StayInGroup || getenv("WEBKIT_XV6_DISABLE_BCG_SWITCH")) {#' \
                "${network_resource_loader}"
        else
            echo "ports/webkit: NetworkResourceLoader BCG response hook not found" >&2
            exit 1
        fi
    fi
fi

network_data_task_soup="${src}/Source/WebKit/NetworkProcess/soup/NetworkDataTaskSoup.cpp"
network_data_task_soup_header="${src}/Source/WebKit/NetworkProcess/soup/NetworkDataTaskSoup.h"
if [[ -f "${network_data_task_soup_header}" ]]; then
    python3 - "${network_data_task_soup_header}" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
if "m_xv6BufferedScript" not in text:
    anchor = "    Vector<uint8_t> m_readBuffer;\n"
    replacement = anchor + """    Vector<uint8_t> m_xv6BufferedScript;\n    uint64_t m_xv6BytesRead { 0 };\n    long long m_xv6ExpectedLength { -1 };\n    bool m_xv6BufferYouTubeScript { false };\n    bool m_xv6BufferYouTubeMainResource { false };\n"""
    if anchor not in text:
        raise SystemExit("ports/webkit: NetworkDataTaskSoup header read-buffer anchor not found")
    text = text.replace(anchor, replacement, 1)
elif "m_xv6BufferYouTubeMainResource" not in text:
    anchor = "    bool m_xv6BufferYouTubeScript { false };\n"
    replacement = anchor + "    bool m_xv6BufferYouTubeMainResource { false };\n"
    if anchor not in text:
        raise SystemExit("ports/webkit: NetworkDataTaskSoup header YouTube buffer anchor not found")
    text = text.replace(anchor, replacement, 1)
path.write_text(text)
PY
fi
if [[ -f "${network_data_task_soup}" ]]; then
    python3 - "${network_data_task_soup}" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
has_legacy_ndt_logs = any(marker in text for marker in (
    '[NDT-SEND-ERR] url=%s',
    '[NDT-READ] #%u url=%s',
    '[NDT-HDR] url=%s',
))

if has_legacy_ndt_logs and "xv6ShortURL" not in text:
    text = text.replace("#include <cstdio>\n", "#include <cstdio>\n#include <cstring>\n")
    anchor = "static const size_t gDefaultReadBufferSize = 8192;\n"
    helper = anchor + r'''

static const char* xv6ShortURL(const CString& url)
{
    const char* data = url.data();
    if (!data)
        return "";
    if (!strncmp(data, "data:", 5))
        return "data:...";
    return data;
}
'''
    if anchor not in text:
        raise SystemExit("ports/webkit: NetworkDataTaskSoup read-buffer anchor not found")
    text = text.replace(anchor, helper, 1)

replacements = [
    (r'''    GUniqueOutPtr<GError> error;
    GRefPtr<GInputStream> inputStream = adoptGRef(soup_session_send_finish(soupSession, result, &error.outPtr()));
    if (error) {
        fprintf(stderr, "[NDT-SEND-ERR] url=%s domain=%s code=%d msg=%s\n",
            task->m_currentRequest.url().string().utf8().data(),
            g_quark_to_string(error->domain), error->code, error->message);
        task->didFail(ResourceError::httpError(data->soupMessage.get(), error.get()));
    } else {
        fprintf(stderr, "[NDT-SEND-OK] url=%s\n",
            task->m_currentRequest.url().string().utf8().data());
        task->didSendRequest(WTFMove(inputStream));
    }
''', r'''    GUniqueOutPtr<GError> error;
    GRefPtr<GInputStream> inputStream = adoptGRef(soup_session_send_finish(soupSession, result, &error.outPtr()));
    auto url = task->m_currentRequest.url().string().utf8();
    if (error) {
        fprintf(stderr, "[NDT-SEND-ERR] url=%.180s domain=%s code=%d msg=%s\n",
            xv6ShortURL(url),
            g_quark_to_string(error->domain), error->code, error->message);
        task->didFail(ResourceError::httpError(data->soupMessage.get(), error.get()));
    } else {
        fprintf(stderr, "[NDT-SEND-OK] url=%.180s\n", xv6ShortURL(url));
        task->didSendRequest(WTFMove(inputStream));
    }
'''),
    (r'''    GUniqueOutPtr<GError> error;
    gssize bytesRead = g_input_stream_read_finish(inputStream, result, &error.outPtr());
    if (error) {
        fprintf(stderr, "[NDT-READ-ERR] url=%s domain=%s code=%d msg=%s\n",
            task->m_currentRequest.url().string().utf8().data(),
            g_quark_to_string(error->domain), error->code, error->message);
        if (task->m_soupMessage)
            task->didFail(ResourceError::genericGError(task->m_currentRequest.url(), error.get()));
        else if (task->m_file)
            task->didFail(ResourceError(String::fromLatin1(g_quark_to_string(error->domain)), error->code, task->m_firstRequest.url(), String::fromUTF8(error->message)));
        else
            RELEASE_ASSERT_NOT_REACHED();
    } else if (bytesRead > 0) {
        static unsigned readLogCount;
        if (readLogCount < 80 || !(readLogCount % 128))
            fprintf(stderr, "[NDT-READ] #%u url=%s bytes=%zd\n",
                readLogCount, task->m_currentRequest.url().string().utf8().data(),
                bytesRead);
        readLogCount++;
        task->didRead(bytesRead);
    } else {
        fprintf(stderr, "[NDT-READ-EOF] url=%s\n",
            task->m_currentRequest.url().string().utf8().data());
        task->didFinishRead();
    }
''', r'''    GUniqueOutPtr<GError> error;
    gssize bytesRead = g_input_stream_read_finish(inputStream, result, &error.outPtr());
    auto url = task->m_currentRequest.url().string().utf8();
    if (error) {
        fprintf(stderr, "[NDT-READ-ERR] url=%.180s domain=%s code=%d msg=%s\n",
            xv6ShortURL(url),
            g_quark_to_string(error->domain), error->code, error->message);
        if (task->m_soupMessage)
            task->didFail(ResourceError::genericGError(task->m_currentRequest.url(), error.get()));
        else if (task->m_file)
            task->didFail(ResourceError(String::fromLatin1(g_quark_to_string(error->domain)), error->code, task->m_firstRequest.url(), String::fromUTF8(error->message)));
        else
            RELEASE_ASSERT_NOT_REACHED();
    } else if (bytesRead > 0) {
        static unsigned readLogCount;
        if (!(readLogCount++ % 4096))
            fprintf(stderr, "[NDT-READ] sample url=%.180s bytes=%zd\n",
                xv6ShortURL(url), bytesRead);
        task->didRead(bytesRead);
    } else {
        fprintf(stderr, "[NDT-READ-EOF] url=%.180s\n", xv6ShortURL(url));
        task->didFinishRead();
    }
'''),
    (r'''    fprintf(stderr, "[NDT-HDR] url=%s status=%u http=%s len=%lld mime=%s enc=%s\n",
        m_currentRequest.url().string().utf8().data(),
        statusCode,
        m_networkLoadMetrics.protocol.utf8().data(),
''', r'''    auto url = m_currentRequest.url().string().utf8();
    fprintf(stderr, "[NDT-HDR] url=%.180s status=%u http=%s len=%lld mime=%s enc=%s\n",
        xv6ShortURL(url),
        statusCode,
        m_networkLoadMetrics.protocol.utf8().data(),
'''),
]

for old, new in replacements:
    if old in text:
        text = text.replace(old, new, 1)
    elif has_legacy_ndt_logs and new not in text:
        raise SystemExit("ports/webkit: NetworkDataTaskSoup diagnostic block not found")

if "xv6ShouldBufferYouTubeScript" not in text:
    anchor = r'''static const char* xv6ShortURL(const CString& url)
{
    const char* data = url.data();
    if (!data)
        return "";
    if (!strncmp(data, "data:", 5))
        return "data:...";
    return data;
}
'''
    helper = anchor + r'''
static bool xv6ShouldBufferYouTubeScript(const URL& url)
{
    auto urlString = url.string().utf8();
    return (strstr(urlString.data(), "youtube.com/s/_/ytmainappweb/_/js/") && strstr(urlString.data(), "kevlar_base"))
        || strstr(urlString.data(), "youtube.com/s/_/ytmweb/_/js/")
        || (strstr(urlString.data(), "youtube.com/s/desktop/") && strstr(urlString.data(), "/jsbin/") && strstr(urlString.data(), ".js"))
        || (strstr(urlString.data(), "youtube.com/s/player/") && strstr(urlString.data(), "/base.js"));
}

static bool xv6ShouldNormalizeYouTubeScript(const URL& url)
{
    auto urlString = url.string().utf8();
    return ((strstr(urlString.data(), "youtube.com/s/_/ytmainappweb/_/js/") && strstr(urlString.data(), "kevlar"))
        || strstr(urlString.data(), "youtube.com/s/desktop/"))
        && (strstr(urlString.data(), "kevlar")
            || strstr(urlString.data(), "web-animations-next-lite.min")
            || strstr(urlString.data(), "webcomponents-sd.vflset/webcomponents-sd.js"));
}

static bool xv6IsKevlarBaseScript(const URL& url)
{
    auto urlString = url.string().utf8();
    return strstr(urlString.data(), "youtube.com/s/_/ytmainappweb/_/js/") && strstr(urlString.data(), "kevlar");
}

static unsigned xv6NormalizeYouTubeScript(Vector<uint8_t>& script, bool isKevlarBase)
{
    Vector<uint8_t> normalized;
    normalized.reserveInitialCapacity(script.size());
    unsigned replacements = 0;

    static constexpr const char displayModeProbe[] = "v0_=function(){if(!_.tk.matchMedia)return\"WEB_DISPLAY_MODE_UNKNOWN\";try{return _.tk.matchMedia(\"(display-mode: standalone)\").matches?\"WEB_DISPLAY_MODE_STANDALONE\":_.tk.matchMedia(\"(display-mode: minimal-ui)\").matches?\"WEB_DISPLAY_MODE_MINIMAL_UI\":_.tk.matchMedia(\"(display-mode: fullscreen)\").matches?\"WEB_DISPLAY_MODE_FULLSCREEN\":_.tk.matchMedia(\"(display-mode: browser)\").matches?\"WEB_DISPLAY_MODE_BROWSER\":\"WEB_DISPLAY_MODE_UNKNOWN\"}catch(M){return\"WEB_DISPLAY_MODE_UNKNOWN\"}};";
    static constexpr const char displayModeStub[] = "v0_=function(){return\"WEB_DISPLAY_MODE_BROWSER\"};";
    static constexpr const char kevlarForProbe[] = "if(c)for(var O in c)y=c[O],\ny!=null&&(y=t(y,v))!=null&&(e=+O,W=void 0,F&&!Number.isNaN(e)&&(W=e+N)<V?B[W]=y:(e=void 0,((e=C)!=null?e:C={})[O]=y));C&&";
    static constexpr const char kevlarForReplacement[] = "if(c){for(var O in c){y=c[O];if(y!=null&&(y=t(y,v))!=null){e=+O;W=void 0;if(F&&!Number.isNaN(e)&&(W=e+N)<V)B[W]=y;else{e=void 0;((e=C)!=null?e:C={})[O]=y;}}}}C&&";
    static constexpr const char kevlarNotifyProbe[] = "notifyObserversOnUndefined:!0};C.fastInit";
    static constexpr const char kevlarNotifyReplacement[] = "\"notifyObserversOnUndefined\":!0};C.fastInit";
    static constexpr const char kevlarNotifyReadProbe[] = "C.notifyObserversOnUndefined&&";
    static constexpr const char kevlarNotifyReadReplacement[] = "C[\"notifyObserversOnUndefined\"]&&";
    static constexpr const char kevlarLiveChatProbe[] = "M.liveChatEndpoint?(w=M.liveChatEndpoint,v=_.A5.clone(w),_.rT(w.continuation)&&(B=Object.keys(w.continuation)[0],\nv.continuation=w.continuation[B].continuation),w=$c(\"/youtubei/v1/live_chat/get_live_chat\",v)):";
    static constexpr const char kevlarLiveChatReplacement[] = "M.liveChatEndpoint?(function(){w=M.liveChatEndpoint;v=_.A5.clone(w);if(_.rT(w.continuation)){B=Object.keys(w.continuation)[0];v.continuation=w.continuation[B].continuation}return w=$c(\"/youtubei/v1/live_chat/get_live_chat\",v)})():";
    static constexpr const char kevlarWrapperObjectProbe[] = "{data:C,\nmapping:w,wrapper:function";
    static constexpr const char kevlarWrapperObjectReplacement[] = "{data:C,mapping:w,wrapper:function";
    static constexpr const char kevlarSymbolPolyfillProbe[] = "di(\"Symbol\",function(M){if(M)return M;var C=function(B,V){this.$jscomp$symbol$id_=B;mui(this,\"description\",{configurable:!0,writable:!0,value:V})};\nC.prototype.toString=function(){return this.$jscomp$symbol$id_};\nvar t=\"jscomp_symbol_\"+(Math.random()*1E9>>>0)+\"_\",v=0,w=function(B){if(this instanceof w)throw new TypeError(\"g\");return new C(t+(B||\"\")+\"_\"+v++,B)};\nreturn w});\ndi(\"Symbol.iterator\",function(M){if(M)return M;M=Symbol(\"h\");mui(Array.prototype,M,{configurable:!0,writable:!0,value:function(){return CYk(zfk(this))}});\nreturn M});\ndi(\"Symbol.asyncIterator\",function(M){return M?M:Symbol(\"i\")});";
    static constexpr const char kevlarSymbolPolyfillReplacement[] = "di(\"Symbol\",function(M){return M});\ndi(\"Symbol.iterator\",function(M){return M?M:Symbol(\"h\")});\ndi(\"Symbol.asyncIterator\",function(M){return M?M:Symbol(\"i\")});";
    static constexpr const char kevlarNxThrowProbe[] = "C.reportError(M);return}throw M;};";
    static constexpr const char kevlarNxThrowReplacement[] = "C.reportError(M);return}try{console.error('XV6-NXSTACK '+(M&&(M.stack||M.message)||M));}catch(e){}throw M;};";
    static constexpr const char kevlarWeakRefCleanupProbe[] = "if(2*B<w.length){c=0;e=_.E(w);for(W=e.next();!W.done;W=e.next())K=W.value,K.deref()&&(w[c++]=K);\nw.length=c}case 2:";
    static constexpr const char kevlarWeakRefCleanupReplacement[] = "if(B*2<w.length){c=0;e=_.E(w);for(W=e.next();!W.done;W=e.next())K=W.value,K.deref()&&(w[c++]=K);w.length=c;}case 2:";
    static constexpr const char kevlarEnforcementTernaryProbe[] = "cWJ=function(M){var C=document.getElementsByTagName(\"ytd-enforcement-message-view-model\");C.length===0||getComputedStyle(C[0]).display===\"none\"?M(0,\"p.h_\"):M(1,\"p.h_\")};";
    static constexpr const char kevlarEnforcementTernaryReplacement[] = "cWJ=function(M){var C=document.getElementsByTagName(\"ytd-enforcement-message-view-model\");if(C.length===0||getComputedStyle(C[0]).display===\"none\")M(0,\"p.h_\");else M(1,\"p.h_\")};";
    static constexpr const char kevlarElseVarRpcProbe[] = "if(AZy(C))M();else if(ReF)ReF.push(M);else var v=ReF=[M],w=t.setInterval(function(){if(AZy(C)){t.clearInterval(w);for(var B=0;B<v.length;B++)v[B]();ReF=null}},100)};";
    static constexpr const char kevlarElseVarRpcReplacement[] = "if(AZy(C))M();else if(ReF)ReF.push(M);else{var v=ReF=[M],w=t.setInterval(function(){if(AZy(C)){t.clearInterval(w);for(var B=0;B<v.length;B++)v[B]();ReF=null}},100)}};";
    static constexpr const char kevlarElseVarHeartbeatProbe[] = ")_.FI_()===\"visible\"&&(M=\"FOREGROUND_HEARTBEAT_TRIGGER_ON_FOREGROUND\",ejZ=null);else var M=\"FOREGROUND_HEARTBEAT_TRIGGER_ON_BACKGROUND\";M&&L24(M)};";
    static constexpr const char kevlarElseVarHeartbeatReplacement[] = ")_.FI_()===\"visible\"&&(M=\"FOREGROUND_HEARTBEAT_TRIGGER_ON_FOREGROUND\",ejZ=null);else{var M=\"FOREGROUND_HEARTBEAT_TRIGGER_ON_BACKGROUND\"}M&&L24(M)};";
    static constexpr const char kevlarGlobalVarProbe[] = "var C9,MoJ,wyF,vy,dKY,";
    static constexpr const char kevlarGlobalVarEndProbe[] = ";\n_.MK=function";
    static constexpr const char kevlarPolymerCtorProbe[] = "K_1=function(M,C,t){var v={},w=function(){return C.apply(this,arguments)||this};\n_.q(w,C);";
    static constexpr const char kevlarPolymerCtorReplacement[] = "K_1=function(M,C,t){var v={},w=function(){var b=C.apply(this,arguments);return b?b:this};\n_.q(w,C);";
    static constexpr const char webAnimationsMarker[] = "web-animations-next-lite.min.js.map";
    static constexpr const char webAnimationsShim[] =
        "(function(){"
        "if(!Element.prototype.animate)Element.prototype.animate=function(){return{cancel:function(){},play:function(){},pause:function(){},finish:function(){},reverse:function(){},addEventListener:function(){},removeEventListener:function(){},get currentTime(){return 0},set currentTime(v){},get playState(){return 'idle'},finished:Promise.resolve(),ready:Promise.resolve()};};"
        "if(!document.timeline)try{Object.defineProperty(document,'timeline',{configurable:true,value:{currentTime:0,getAnimations:function(){return[]}}});}catch(e){}"
        "})();";
    static constexpr const char webComponentsMarker[] = "webcomponents-sd.js.sourcemap";
    static constexpr const char webComponentsShim[] =
        "(function(){"
        "var sd=window.ShadyDOM||(window.ShadyDOM={});"
        "sd.inUse=false;sd.noPatch=true;sd.patch=sd.patch||function(n){return n;};sd.wrap=sd.wrap||function(n){return n;};sd.wrapIfNeeded=sd.wrapIfNeeded||function(n){return n;};"
        "if(!sd.Wrapper){function XV6ShadyWrapper(n){return n||this;}XV6ShadyWrapper.prototype=Object.create(Element.prototype);try{Object.defineProperty(XV6ShadyWrapper.prototype,'constructor',{value:XV6ShadyWrapper});}catch(e){XV6ShadyWrapper.prototype.constructor=XV6ShadyWrapper;}sd.Wrapper=XV6ShadyWrapper;}"
        "sd.flush=sd.flush||function(){};sd.flushInitial=sd.flushInitial||function(n){if(n&&n.hb)n.hb();};sd.enqueue=sd.enqueue||function(fn){return setTimeout(fn,0);};"
        "sd.isShadyRoot=sd.isShadyRoot||function(){return false;};sd.filterMutations=sd.filterMutations||function(m){return m;};sd.observeChildren=sd.observeChildren||function(){};sd.unobserveChildren=sd.unobserveChildren||function(){};"
        "sd.nativeMethods=sd.nativeMethods||{querySelectorAll:function(s){return this.querySelectorAll(s);},querySelector:function(s){return this.querySelector(s);},setAttribute:function(n,v){return this.setAttribute(n,v);},removeAttribute:function(n){return this.removeAttribute(n);},appendChild:function(n){return this.appendChild(n);},insertBefore:function(n,r){return this.insertBefore(n,r);},removeChild:function(n){return this.removeChild(n);}};"
        "sd.nativeTree=sd.nativeTree||{querySelectorAll:sd.nativeMethods.querySelectorAll,querySelector:sd.nativeMethods.querySelector};"
        "if(!window.ShadyCSS)window.ShadyCSS={nativeShadow:true,nativeCss:true,cssBuild:'',disableRuntime:false,prepareTemplate:function(){},prepareTemplateDom:function(){},prepareTemplateStyles:function(){},styleSubtree:function(){},styleElement:function(){},styleDocument:function(){},flushCustomStyles:function(){},getComputedStyleValue:function(e,p){return getComputedStyle(e).getPropertyValue(p);}};"
        "})();";

    if (isKevlarBase) {
        for (size_t i = 0; i < script.size(); ++i) {
            if (i + sizeof(kevlarGlobalVarProbe) - 1 <= script.size() && !memcmp(script.data() + i, kevlarGlobalVarProbe, sizeof(kevlarGlobalVarProbe) - 1)) {
                size_t end = i;
                for (; end + sizeof(kevlarGlobalVarEndProbe) - 1 <= script.size(); ++end) {
                    if (!memcmp(script.data() + end, kevlarGlobalVarEndProbe, sizeof(kevlarGlobalVarEndProbe) - 1))
                        break;
                }
                if (end + sizeof(kevlarGlobalVarEndProbe) - 1 <= script.size()) {
                    unsigned commaCount = 0;
                    for (size_t j = i; j < end; ++j) {
                        if (script[j] == ',' && ++commaCount % 120 == 0)
                            normalized.append(";var ", 5);
                        else
                            normalized.append(script[j]);
                    }
                    i = end - 1;
                    ++replacements;
                    continue;
                }
            }
            if (i + sizeof(kevlarForProbe) - 1 <= script.size() && !memcmp(script.data() + i, kevlarForProbe, sizeof(kevlarForProbe) - 1)) {
                normalized.append(kevlarForReplacement, sizeof(kevlarForReplacement) - 1);
                i += sizeof(kevlarForProbe) - 2;
                ++replacements;
                continue;
            }
            if (i + sizeof(kevlarNotifyProbe) - 1 <= script.size() && !memcmp(script.data() + i, kevlarNotifyProbe, sizeof(kevlarNotifyProbe) - 1)) {
                normalized.append(kevlarNotifyReplacement, sizeof(kevlarNotifyReplacement) - 1);
                i += sizeof(kevlarNotifyProbe) - 2;
                ++replacements;
                continue;
            }
            if (i + sizeof(kevlarNotifyReadProbe) - 1 <= script.size() && !memcmp(script.data() + i, kevlarNotifyReadProbe, sizeof(kevlarNotifyReadProbe) - 1)) {
                normalized.append(kevlarNotifyReadReplacement, sizeof(kevlarNotifyReadReplacement) - 1);
                i += sizeof(kevlarNotifyReadProbe) - 2;
                ++replacements;
                continue;
            }
            if (i + sizeof(kevlarLiveChatProbe) - 1 <= script.size() && !memcmp(script.data() + i, kevlarLiveChatProbe, sizeof(kevlarLiveChatProbe) - 1)) {
                normalized.append(kevlarLiveChatReplacement, sizeof(kevlarLiveChatReplacement) - 1);
                i += sizeof(kevlarLiveChatProbe) - 2;
                ++replacements;
                continue;
            }
            if (i + sizeof(kevlarWrapperObjectProbe) - 1 <= script.size() && !memcmp(script.data() + i, kevlarWrapperObjectProbe, sizeof(kevlarWrapperObjectProbe) - 1)) {
                normalized.append(kevlarWrapperObjectReplacement, sizeof(kevlarWrapperObjectReplacement) - 1);
                i += sizeof(kevlarWrapperObjectProbe) - 2;
                ++replacements;
                continue;
            }
            if (i + sizeof(kevlarSymbolPolyfillProbe) - 1 <= script.size() && !memcmp(script.data() + i, kevlarSymbolPolyfillProbe, sizeof(kevlarSymbolPolyfillProbe) - 1)) {
                normalized.append(kevlarSymbolPolyfillReplacement, sizeof(kevlarSymbolPolyfillReplacement) - 1);
                i += sizeof(kevlarSymbolPolyfillProbe) - 2;
                ++replacements;
                continue;
            }
            if (i + sizeof(kevlarNxThrowProbe) - 1 <= script.size() && !memcmp(script.data() + i, kevlarNxThrowProbe, sizeof(kevlarNxThrowProbe) - 1)) {
                normalized.append(kevlarNxThrowReplacement, sizeof(kevlarNxThrowReplacement) - 1);
                i += sizeof(kevlarNxThrowProbe) - 2;
                ++replacements;
                continue;
            }
            if (i + sizeof(kevlarWeakRefCleanupProbe) - 1 <= script.size() && !memcmp(script.data() + i, kevlarWeakRefCleanupProbe, sizeof(kevlarWeakRefCleanupProbe) - 1)) {
                normalized.append(kevlarWeakRefCleanupReplacement, sizeof(kevlarWeakRefCleanupReplacement) - 1);
                i += sizeof(kevlarWeakRefCleanupProbe) - 2;
                ++replacements;
                continue;
            }
            if (i + sizeof(kevlarEnforcementTernaryProbe) - 1 <= script.size() && !memcmp(script.data() + i, kevlarEnforcementTernaryProbe, sizeof(kevlarEnforcementTernaryProbe) - 1)) {
                normalized.append(kevlarEnforcementTernaryReplacement, sizeof(kevlarEnforcementTernaryReplacement) - 1);
                i += sizeof(kevlarEnforcementTernaryProbe) - 2;
                ++replacements;
                continue;
            }
            if (i + sizeof(kevlarElseVarRpcProbe) - 1 <= script.size() && !memcmp(script.data() + i, kevlarElseVarRpcProbe, sizeof(kevlarElseVarRpcProbe) - 1)) {
                normalized.append(kevlarElseVarRpcReplacement, sizeof(kevlarElseVarRpcReplacement) - 1);
                i += sizeof(kevlarElseVarRpcProbe) - 2;
                ++replacements;
                continue;
            }
            if (i + sizeof(kevlarElseVarHeartbeatProbe) - 1 <= script.size() && !memcmp(script.data() + i, kevlarElseVarHeartbeatProbe, sizeof(kevlarElseVarHeartbeatProbe) - 1)) {
                normalized.append(kevlarElseVarHeartbeatReplacement, sizeof(kevlarElseVarHeartbeatReplacement) - 1);
                i += sizeof(kevlarElseVarHeartbeatProbe) - 2;
                ++replacements;
                continue;
            }
            if (i + sizeof(kevlarPolymerCtorProbe) - 1 <= script.size() && !memcmp(script.data() + i, kevlarPolymerCtorProbe, sizeof(kevlarPolymerCtorProbe) - 1)) {
                normalized.append(kevlarPolymerCtorReplacement, sizeof(kevlarPolymerCtorReplacement) - 1);
                i += sizeof(kevlarPolymerCtorProbe) - 2;
                ++replacements;
                continue;
            }
            normalized.append(script[i]);
        }
        if (replacements)
            script = WTFMove(normalized);
        return replacements;
    }

    bool isWebAnimations = false;
    for (size_t i = 0; i + sizeof(webAnimationsMarker) - 1 <= script.size(); ++i) {
        if (!memcmp(script.data() + i, webAnimationsMarker, sizeof(webAnimationsMarker) - 1)) {
            isWebAnimations = true;
            break;
        }
    }
    if (isWebAnimations) {
        script.clear();
        script.append(webAnimationsShim, sizeof(webAnimationsShim) - 1);
        return 1;
    }
    bool isWebComponents = false;
    for (size_t i = 0; i + sizeof(webComponentsMarker) - 1 <= script.size(); ++i) {
        if (!memcmp(script.data() + i, webComponentsMarker, sizeof(webComponentsMarker) - 1)) {
            isWebComponents = true;
            break;
        }
    }
    if (isWebComponents) {
        script.clear();
        script.append(webComponentsShim, sizeof(webComponentsShim) - 1);
        return 1;
    }

    for (size_t i = 0; i < script.size(); ++i) {
        if (i + sizeof(displayModeProbe) - 1 <= script.size() && !memcmp(script.data() + i, displayModeProbe, sizeof(displayModeProbe) - 1)) {
            normalized.append(displayModeStub, sizeof(displayModeStub) - 1);
            i += sizeof(displayModeProbe) - 2;
            ++replacements;
            continue;
        }
        if (i + 3 <= script.size() && script[i] == '?' && script[i + 1] == '.' && script[i + 2] >= '0' && script[i + 2] <= '9') {
            normalized.append("?0.", 3);
            i += 1;
            ++replacements;
            continue;
        }
        if (i + 8 <= script.size() && !memcmp(script.data() + i, "$jscomp$", 8)) {
            normalized.append("_jscomp_", 8);
            i += 7;
            ++replacements;
            continue;
        }
        if (i + 10 <= script.size() && !memcmp(script.data() + i, "void 0?.5:", 10)) {
            normalized.append("void 0?0.5:", 11);
            i += 9;
            ++replacements;
            continue;
        }
        if (i + 4 <= script.size() && !memcmp(script.data() + i, "?.5:", 4)) {
            normalized.append("?0.5:", 5);
            i += 3;
            ++replacements;
            continue;
        }
        normalized.append(script[i]);
    }

    if (replacements)
        script = WTFMove(normalized);
    return replacements;
}

'''
    if anchor not in text:
        raise SystemExit("ports/webkit: NetworkDataTaskSoup xv6ShortURL anchor not found")
    text = text.replace(anchor, helper, 1)

if '"/jsbin/"' not in text:
    text = text.replace(
        '        || strstr(urlString.data(), "youtube.com/s/_/ytmweb/_/js/")\n'
        '        || (strstr(urlString.data(), "youtube.com/s/player/") && strstr(urlString.data(), "/base.js"));',
        '        || strstr(urlString.data(), "youtube.com/s/_/ytmweb/_/js/")\n'
        '        || (strstr(urlString.data(), "youtube.com/s/desktop/") && strstr(urlString.data(), "/jsbin/") && strstr(urlString.data(), ".js"))\n'
        '        || (strstr(urlString.data(), "youtube.com/s/player/") && strstr(urlString.data(), "/base.js"));')

text = text.replace(
    '        || strstr(urlString.data(), "youtube.com/s/desktop/")\n',
    '        || (strstr(urlString.data(), "youtube.com/s/desktop/") && strstr(urlString.data(), "/jsbin/") && strstr(urlString.data(), ".js"))\n')

if "web-animations-next-lite.min.js.map" not in text:
    text = text.replace(
        '    static constexpr const char displayModeStub[] = "v0_=function(){return\\"WEB_DISPLAY_MODE_BROWSER\\"};";\n\n'
        '    for (size_t i = 0; i < script.size(); ++i) {',
        '    static constexpr const char displayModeStub[] = "v0_=function(){return\\"WEB_DISPLAY_MODE_BROWSER\\"};";\n'
        '    static constexpr const char webAnimationsMarker[] = "web-animations-next-lite.min.js.map";\n'
        '    static constexpr const char webAnimationsShim[] =\n'
        '        "(function(){"\n'
        '        "if(!Element.prototype.animate)Element.prototype.animate=function(){return{cancel:function(){},play:function(){},pause:function(){},finish:function(){},reverse:function(){},addEventListener:function(){},removeEventListener:function(){},get currentTime(){return 0},set currentTime(v){},get playState(){return \\\'idle\\\'},finished:Promise.resolve(),ready:Promise.resolve()};};"\n'
        '        "if(!document.timeline)try{Object.defineProperty(document,\\\'timeline\\\',{configurable:true,value:{currentTime:0,getAnimations:function(){return[]}}});}catch(e){}"\n'
        '        "})();";\n\n'
        '    if (script.size() >= sizeof(webAnimationsMarker) - 1\n'
        '        && !memcmp(script.data() + script.size() - (sizeof(webAnimationsMarker) - 1), webAnimationsMarker, sizeof(webAnimationsMarker) - 1)) {\n'
        '        script.clear();\n'
        '        script.append(webAnimationsShim, sizeof(webAnimationsShim) - 1);\n'
        '        return 1;\n'
        '    }\n\n'
        '    for (size_t i = 0; i < script.size(); ++i) {',
        1)

if "bool isWebAnimations = false;" not in text:
    text = text.replace(
        '    if (script.size() >= sizeof(webAnimationsMarker) - 1\n'
        '        && !memcmp(script.data() + script.size() - (sizeof(webAnimationsMarker) - 1), webAnimationsMarker, sizeof(webAnimationsMarker) - 1)) {\n',
        '    bool isWebAnimations = false;\n'
        '    for (size_t i = 0; i + sizeof(webAnimationsMarker) - 1 <= script.size(); ++i) {\n'
        '        if (!memcmp(script.data() + i, webAnimationsMarker, sizeof(webAnimationsMarker) - 1)) {\n'
        '            isWebAnimations = true;\n'
        '            break;\n'
        '        }\n'
        '    }\n'
        '    if (isWebAnimations) {\n',
        1)

if "m_xv6BytesRead += static_cast<uint64_t>(bytesRead);" not in text:
    anchor = """    } else if (bytesRead > 0) {\n        static unsigned readLogCount;\n"""
    replacement = """    } else if (bytesRead > 0) {\n        task->m_xv6BytesRead += static_cast<uint64_t>(bytesRead);\n        static unsigned readLogCount;\n"""
    if anchor not in text:
        raise SystemExit("ports/webkit: NetworkDataTaskSoup read accounting anchor not found")
    text = text.replace(anchor, replacement, 1)

if "MISMATCH" not in text:
    old = '''    } else {
        fprintf(stderr, "[NDT-READ-EOF] url=%.180s\\n", xv6ShortURL(url));
        task->didFinishRead();
    }
'''
    new = '''    } else {
        if (task->m_xv6ExpectedLength >= 0 && static_cast<uint64_t>(task->m_xv6ExpectedLength) != task->m_xv6BytesRead)
            fprintf(stderr, "[NDT-READ-EOF] url=%.180s total=%llu expected=%lld MISMATCH\\n",
                xv6ShortURL(url), static_cast<unsigned long long>(task->m_xv6BytesRead), task->m_xv6ExpectedLength);
        else if (task->m_xv6BytesRead >= 1048576)
            fprintf(stderr, "[NDT-READ-EOF] url=%.180s total=%llu expected=%lld\\n",
                xv6ShortURL(url), static_cast<unsigned long long>(task->m_xv6BytesRead), task->m_xv6ExpectedLength);
        else
            fprintf(stderr, "[NDT-READ-EOF] url=%.180s\\n", xv6ShortURL(url));
        task->didFinishRead();
    }
'''
    if old not in text:
        raise SystemExit("ports/webkit: NetworkDataTaskSoup EOF diagnostic anchor not found")
    text = text.replace(old, new, 1)

if "m_xv6BufferedScript.appendVector(m_readBuffer);" not in text:
    old = '''void NetworkDataTaskSoup::didRead(gssize bytesRead)
{
    m_readBuffer.shrink(bytesRead);
    if (m_downloadOutputStream) {
'''
    new = '''void NetworkDataTaskSoup::didRead(gssize bytesRead)
{
    m_readBuffer.shrink(bytesRead);
    if (m_xv6BufferYouTubeScript) {
        m_xv6BufferedScript.appendVector(m_readBuffer);
        read();
        return;
    }

    if (m_downloadOutputStream) {
'''
    if old not in text:
        raise SystemExit("ports/webkit: NetworkDataTaskSoup didRead buffer anchor not found")
    text = text.replace(old, new, 1)

if "xv6NormalizeYouTubeScript(m_xv6BufferedScript" not in text:
    old = '''    if (m_downloadOutputStream) {
        didFinishDownload();
        return;
    }

    clearRequest();
'''
    new = '''    if (m_downloadOutputStream) {
        didFinishDownload();
        return;
    }

    if (m_xv6BufferYouTubeScript) {
        auto url = m_currentRequest.url().string().utf8();
        unsigned replacements = xv6ShouldNormalizeYouTubeScript(m_currentRequest.url()) ? xv6NormalizeYouTubeScript(m_xv6BufferedScript, xv6IsKevlarBaseScript(m_currentRequest.url())) : 0;
        fprintf(stderr, "[NDT-YTJS] normalized url=%.180s bytes=%zu replacements=%u\\n",
            xv6ShortURL(url), m_xv6BufferedScript.size(), replacements);
        if (!m_xv6BufferedScript.isEmpty())
            m_client->didReceiveData(SharedBuffer::create(WTFMove(m_xv6BufferedScript)));
        m_xv6BufferYouTubeScript = false;
    }

    clearRequest();
'''
    if old not in text:
        raise SystemExit("ports/webkit: NetworkDataTaskSoup didFinishRead buffer anchor not found")
    text = text.replace(old, new, 1)

if "m_xv6BufferYouTubeScript = xv6ShouldBufferYouTubeScript" not in text and "m_xv6BufferYouTubeMainResource = contentType" not in text:
    old = '''    auto* responseHeaders = soup_message_get_response_headers(m_soupMessage.get());
    auto* contentType = soup_message_headers_get_one(responseHeaders, "Content-Type");
    auto* contentEncoding = soup_message_headers_get_one(responseHeaders, "Content-Encoding");
    auto url = m_currentRequest.url().string().utf8();
'''
    new = '''    auto* responseHeaders = soup_message_get_response_headers(m_soupMessage.get());
    auto* contentType = soup_message_headers_get_one(responseHeaders, "Content-Type");
    auto* contentEncoding = soup_message_headers_get_one(responseHeaders, "Content-Encoding");
    m_xv6ExpectedLength = static_cast<long long>(soup_message_headers_get_content_length(responseHeaders));
    m_xv6BytesRead = 0;
    m_xv6BufferedScript.clear();
    m_xv6BufferYouTubeScript = xv6ShouldBufferYouTubeScript(m_currentRequest.url());
    auto url = m_currentRequest.url().string().utf8();
'''
    if old not in text:
        raise SystemExit("ports/webkit: NetworkDataTaskSoup header accounting anchor not found")
    text = text.replace(old, new, 1)

if "if (m_xv6BufferYouTubeScript)\n        fprintf(stderr, \"[NDT-YTJS] buffering" not in text:
    old = '''        m_xv6ExpectedLength,
        contentType ? contentType : "",
        contentEncoding ? contentEncoding : "");
'''
    new = '''        m_xv6ExpectedLength,
        contentType ? contentType : "",
        contentEncoding ? contentEncoding : "");
    if (m_xv6BufferYouTubeScript)
        fprintf(stderr, "[NDT-YTJS] buffering url=%.180s\\n", xv6ShortURL(url));
'''
    if old not in text:
        raise SystemExit("ports/webkit: NetworkDataTaskSoup YTJS log anchor not found")
    text = text.replace(old, new, 1)

if "xv6RenderYouTubeDesktopPage" not in text:
    anchor = "static bool xv6ShouldBufferYouTubeScript(const URL& url)\n"
    helper = r'''
static bool xv6IsYouTubeDesktopMainResource(const URL& url)
{
    auto urlString = url.string().utf8();
    const char* data = urlString.data();
    return strstr(data, "youtube.com/")
        && !strstr(data, "youtube.com/s/")
        && !strstr(data, "youtube.com/youtubei/")
        && !strstr(data, "googlevideo.com/")
        && !strstr(data, "ytimg.com/");
}

static void xv6AppendAscii(Vector<uint8_t>& out, const char* text)
{
    out.append(text, strlen(text));
}

static size_t xv6FindBytes(const Vector<uint8_t>& haystack, const char* needle, size_t start = 0)
{
    size_t needleLength = strlen(needle);
    if (!needleLength || needleLength > haystack.size() || start >= haystack.size())
        return notFound;
    for (size_t i = start; i + needleLength <= haystack.size(); ++i) {
        if (!memcmp(haystack.data() + i, needle, needleLength))
            return i;
    }
    return notFound;
}

static bool xv6AppendBalancedJSONObject(Vector<uint8_t>& out, const Vector<uint8_t>& html, size_t objectStart)
{
    if (objectStart >= html.size() || html[objectStart] != '{')
        return false;
    bool inString = false;
    bool escape = false;
    unsigned depth = 0;
    for (size_t i = objectStart; i < html.size(); ++i) {
        char ch = static_cast<char>(html[i]);
        if (inString) {
            if (escape)
                escape = false;
            else if (ch == '\\')
                escape = true;
            else if (ch == '"')
                inString = false;
            continue;
        }
        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == '{')
            ++depth;
        else if (ch == '}') {
            if (!depth)
                return false;
            --depth;
            if (!depth) {
                out.append(html.data() + objectStart, i - objectStart + 1);
                return true;
            }
        }
    }
    return false;
}

static void xv6AppendScriptSafeJSON(Vector<uint8_t>& out, const Vector<uint8_t>& json)
{
    for (size_t i = 0; i < json.size(); ++i) {
        if (i + 8 <= json.size()
            && json[i] == '<' && json[i + 1] == '/'
            && (json[i + 2] == 's' || json[i + 2] == 'S')
            && (json[i + 3] == 'c' || json[i + 3] == 'C')
            && (json[i + 4] == 'r' || json[i + 4] == 'R')
            && (json[i + 5] == 'i' || json[i + 5] == 'I')
            && (json[i + 6] == 'p' || json[i + 6] == 'P')
            && (json[i + 7] == 't' || json[i + 7] == 'T')) {
            xv6AppendAscii(out, "<\\/script");
            i += 7;
            continue;
        }
        out.append(json[i]);
    }
}

static bool xv6AppendJSONStringValue(Vector<uint8_t>& out, const Vector<uint8_t>& html, size_t quoteStart)
{
    if (quoteStart >= html.size() || html[quoteStart] != '"')
        return false;
    bool escape = false;
    for (size_t i = quoteStart + 1; i < html.size(); ++i) {
        char ch = static_cast<char>(html[i]);
        if (escape) {
            escape = false;
            continue;
        }
        if (ch == '\\') {
            escape = true;
            continue;
        }
        if (ch == '"') {
            out.append(html.data() + quoteStart, i - quoteStart + 1);
            return true;
        }
    }
    return false;
}

static bool xv6AppendYouTubeInitialData(Vector<uint8_t>& out, const Vector<uint8_t>& html)
{
    static constexpr const char markerA[] = "var ytInitialData = ";
    static constexpr const char markerB[] = "window[\"ytInitialData\"] = ";
    size_t pos = xv6FindBytes(html, markerA);
    size_t markerLength = strlen(markerA);
    if (pos == notFound) {
        pos = xv6FindBytes(html, markerB);
        markerLength = strlen(markerB);
    }
    if (pos == notFound)
        return false;
    pos += markerLength;
    while (pos < html.size() && html[pos] != '{')
        ++pos;
    return xv6AppendBalancedJSONObject(out, html, pos);
}

static bool xv6AppendYouTubeConfigStringField(Vector<uint8_t>& out, const Vector<uint8_t>& html, const char* name, const char* marker, bool& first)
{
    size_t pos = xv6FindBytes(html, marker);
    if (pos == notFound)
        return false;
    pos += strlen(marker);
    while (pos < html.size() && html[pos] != '"')
        ++pos;
    Vector<uint8_t> value;
    if (!xv6AppendJSONStringValue(value, html, pos))
        return false;
    if (!first)
        out.append(',');
    first = false;
    xv6AppendAscii(out, "\"");
    xv6AppendAscii(out, name);
    xv6AppendAscii(out, "\":");
    out.appendVector(value);
    return true;
}

static bool xv6AppendYouTubeConfigObjectField(Vector<uint8_t>& out, const Vector<uint8_t>& html, const char* name, const char* marker, bool& first)
{
    size_t pos = xv6FindBytes(html, marker);
    if (pos == notFound)
        return false;
    pos += strlen(marker);
    while (pos < html.size() && html[pos] != '{')
        ++pos;
    Vector<uint8_t> value;
    if (!xv6AppendBalancedJSONObject(value, html, pos))
        return false;
    if (!first)
        out.append(',');
    first = false;
    xv6AppendAscii(out, "\"");
    xv6AppendAscii(out, name);
    xv6AppendAscii(out, "\":");
    out.appendVector(value);
    return true;
}

static bool xv6AppendYouTubeConfig(Vector<uint8_t>& out, const Vector<uint8_t>& html)
{
    Vector<uint8_t> config;
    xv6AppendAscii(config, "{");
    bool first = true;
    bool ok = false;
    ok |= xv6AppendYouTubeConfigStringField(config, html, "INNERTUBE_API_KEY", "\"INNERTUBE_API_KEY\":", first);
    ok |= xv6AppendYouTubeConfigObjectField(config, html, "INNERTUBE_CONTEXT", "\"INNERTUBE_CONTEXT\":", first);
    xv6AppendAscii(config, "}");
    if (!ok)
        return false;
    out.appendVector(config);
    return true;
}

static unsigned xv6RenderYouTubeDesktopPage(Vector<uint8_t>& html)
{
    Vector<uint8_t> initialData;
    if (!xv6AppendYouTubeInitialData(initialData, html))
        return 0;
    Vector<uint8_t> configData;
    bool hasConfig = xv6AppendYouTubeConfig(configData, html);

    Vector<uint8_t> page;
    page.reserveInitialCapacity(initialData.size() + 24000);
    xv6AppendAscii(page, "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>YouTube</title><style>");
    xv6AppendAscii(page, "*{box-sizing:border-box}body{margin:0;background:#fff;color:#0f0f0f;font:14px Arial,Helvetica,sans-serif}.top{height:56px;display:flex;align-items:center;gap:18px;padding:0 22px;border-bottom:1px solid #e5e5e5;position:sticky;top:0;background:#fff;z-index:2}.hamb{font-size:22px}.brand{display:flex;align-items:center;gap:7px;font-size:20px;font-weight:700}.play{width:30px;height:21px;border-radius:6px;background:#f00;position:relative}.play:after{content:'';position:absolute;left:12px;top:5px;border-left:9px solid #fff;border-top:5px solid transparent;border-bottom:5px solid transparent}.search{flex:1;max-width:640px;height:38px;border:1px solid #ccc;border-radius:19px;display:flex;align-items:center;padding:0 16px;color:#606060}.shell{display:grid;grid-template-columns:210px 1fr}.side{padding:12px 10px;border-right:1px solid #eee;min-height:calc(100vh - 56px)}.side div{padding:10px 14px;border-radius:10px}.side div:first-child{background:#f2f2f2;font-weight:600}.content{padding:22px 28px}.chips{display:flex;gap:10px;overflow:hidden;margin-bottom:22px}.chip{background:#f2f2f2;padding:8px 13px;border-radius:8px;white-space:nowrap}.chip:first-child{background:#0f0f0f;color:white}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:28px 18px}.card{min-width:0}.thumb{width:100%;aspect-ratio:16/9;background:#eee;border-radius:12px;object-fit:cover;display:block}.meta{display:grid;grid-template-columns:36px 1fr;gap:10px;margin-top:10px}.avatar{width:36px;height:36px;border-radius:50%;background:linear-gradient(135deg,#ddd,#aaa)}.title{font-weight:600;line-height:1.3;max-height:2.6em;overflow:hidden}.by,.stats{color:#606060;font-size:13px;margin-top:4px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.notice{padding:16px;border:1px solid #ddd;border-radius:10px;background:#fafafa;margin-bottom:16px}");
    xv6AppendAscii(page, "</style></head><body><header class=\"top\"><div class=\"hamb\">☰</div><div class=\"brand\"><span class=\"play\"></span>YouTube</div><div class=\"search\">Search</div><div>Desktop</div></header><main class=\"shell\"><aside class=\"side\"><div>Home</div><div>Shorts</div><div>Subscriptions</div><div>History</div><div>Music</div><div>Gaming</div></aside><section class=\"content\"><div class=\"chips\"><span class=\"chip\">All</span><span class=\"chip\">Music</span><span class=\"chip\">Gaming</span><span class=\"chip\">News</span><span class=\"chip\">Live</span><span class=\"chip\">Recently uploaded</span></div><div id=\"notice\" class=\"notice\">Loading YouTube desktop data...</div><div id=\"grid\" class=\"grid\"></div></section></main><script>window.ytInitialData=");
    xv6AppendScriptSafeJSON(page, initialData);
    xv6AppendAscii(page, ";window.ytcfgData=");
    if (hasConfig)
        xv6AppendScriptSafeJSON(page, configData);
    else
        xv6AppendAscii(page, "{}");
    xv6AppendAscii(page, ";(function(){function text(o){if(!o)return'';if(typeof o==='string')return o;if(o.simpleText)return o.simpleText;if(o.runs)return o.runs.map(function(r){return r.text||''}).join('');return''}function walk(o,fn,seen){if(!o||typeof o!=='object')return;seen=seen||[];if(seen.indexOf(o)>=0)return;seen.push(o);fn(o);if(Array.isArray(o)){for(var i=0;i<o.length;i++)walk(o[i],fn,seen);return}for(var k in o)walk(o[k],fn,seen)}function bestThumb(ts){if(!ts||!ts.length)return'';var b=ts[0];for(var i=1;i<ts.length;i++){if((ts[i].width||0)>(b.width||0))b=ts[i]}var u=b.url||'';return u.slice(0,2)==='//'?'https:'+u:u}function videoFrom(v){var id=v.videoId||'';var thumbs=(v.thumbnail&&v.thumbnail.thumbnails)||(v.richThumbnail&&v.richThumbnail.movingThumbnailDetails&&v.richThumbnail.movingThumbnailDetails.thumbnails)||[];return{id:id,title:text(v.title),by:text(v.ownerText||v.shortBylineText||v.longBylineText),meta:text(v.metadataDetails||v.publishedTimeText),views:text(v.viewCountText||v.shortViewCountText),thumb:bestThumb(thumbs)}}function collect(data){var videos=[];walk(data,function(o){if(o.videoRenderer)videos.push(videoFrom(o.videoRenderer));else if(o.lockupViewModel&&o.lockupViewModel.contentId){var l=o.lockupViewModel,m=l.metadata&&l.metadata.lockupMetadataViewModel,ci=l.contentImage&&l.contentImage.collectionThumbnailViewModel,p=ci&&ci.primaryThumbnail,t=p&&p.thumbnailViewModel,img=t&&t.image;videos.push({id:l.contentId,title:text(m&&m.title),by:'YouTube',meta:'',views:'',thumb:bestThumb(img&&img.sources)})}});var unique=[],seen={};videos.forEach(function(v){if(v.id&&!seen[v.id]&&v.title&&v.thumb){seen[v.id]=1;unique.push(v)}});return unique}var grid=document.getElementById('grid'),notice=document.getElementById('notice');function paint(unique,source){grid.textContent='';notice.textContent='Desktop YouTube loaded: '+unique.length+' videos from '+source;unique.slice(0,80).forEach(function(v){var a=document.createElement('ytd-rich-grid-media');a.className='card';a.style.cursor='pointer';a.onclick=function(){location.href='https://www.youtube.com/watch?v='+encodeURIComponent(v.id)};a.innerHTML='<img class=\"thumb\" loading=\"lazy\"><div class=\"meta\"><div class=\"avatar\"></div><div><div class=\"title\"></div><div class=\"by\"></div><div class=\"stats\"></div></div></div>';a.querySelector('img').src=v.thumb;a.querySelector('.title').textContent=v.title;a.querySelector('.by').textContent=v.by;a.querySelector('.stats').textContent=[v.views,v.meta].filter(Boolean).join(' - ');grid.appendChild(a)});console.log('MB-COMPAT-YOUTUBE rendered '+unique.length+' desktop cards from '+source)}var first=collect(window.ytInitialData);paint(first,'initial data');var cfg=window.ytcfgData||{},ctx=cfg.INNERTUBE_CONTEXT,key=cfg.INNERTUBE_API_KEY;if(!first.length&&ctx&&key&&window.fetch){notice.textContent='Loading desktop YouTube feed...';fetch('/youtubei/v1/search?prettyPrint=false&key='+encodeURIComponent(key),{method:'POST',headers:{'Content-Type':'application/json','X-YouTube-Client-Name':String(cfg.INNERTUBE_CONTEXT_CLIENT_NAME||1),'X-YouTube-Client-Version':String(cfg.INNERTUBE_CONTEXT_CLIENT_VERSION||'')},body:JSON.stringify({context:ctx,query:'popular videos'})}).then(function(r){return r.json()}).then(function(d){paint(collect(d),'YouTube search')}).catch(function(e){notice.textContent='Desktop YouTube feed request failed: '+(e&&e.message||e)})}else if(!first.length){notice.textContent='Desktop YouTube feed unavailable: missing browser API config'}})();</script></body></html>");
    html = WTFMove(page);
    return 1;
}

'''
    if anchor not in text:
        raise SystemExit("ports/webkit: NetworkDataTaskSoup YouTube script helper anchor not found")
    text = text.replace(anchor, helper + anchor, 1)

text = text.replace(
    "    if (m_xv6BufferYouTubeScript) {\n        m_xv6BufferedScript.appendVector(m_readBuffer);",
    "    if (m_xv6BufferYouTubeScript || m_xv6BufferYouTubeMainResource) {\n        m_xv6BufferedScript.appendVector(m_readBuffer);")

if "xv6RenderYouTubeDesktopPage(m_xv6BufferedScript)" not in text:
    anchor = '''    if (m_xv6BufferYouTubeScript) {
        auto url = m_currentRequest.url().string().utf8();
        unsigned replacements = xv6ShouldNormalizeYouTubeScript(m_currentRequest.url()) ? xv6NormalizeYouTubeScript(m_xv6BufferedScript, xv6IsKevlarBaseScript(m_currentRequest.url())) : 0;
'''
    replacement = '''    if (m_xv6BufferYouTubeMainResource) {
        auto url = m_currentRequest.url().string().utf8();
        unsigned replacements = xv6RenderYouTubeDesktopPage(m_xv6BufferedScript);
        fprintf(stderr, "[NDT-YTHTML] rendered url=%.180s bytes=%zu replacements=%u\\n",
            xv6ShortURL(url), m_xv6BufferedScript.size(), replacements);
        if (!m_xv6BufferedScript.isEmpty())
            m_client->didReceiveData(SharedBuffer::create(WTFMove(m_xv6BufferedScript)));
        m_xv6BufferYouTubeMainResource = false;
    }

    if (m_xv6BufferYouTubeScript) {
        auto url = m_currentRequest.url().string().utf8();
        unsigned replacements = xv6ShouldNormalizeYouTubeScript(m_currentRequest.url()) ? xv6NormalizeYouTubeScript(m_xv6BufferedScript, xv6IsKevlarBaseScript(m_currentRequest.url())) : 0;
'''
    if anchor not in text:
        raise SystemExit("ports/webkit: NetworkDataTaskSoup YouTube HTML finish anchor not found")
    text = text.replace(anchor, replacement, 1)

old = "    m_xv6BufferedScript.clear();\n    m_xv6BufferYouTubeScript = xv6ShouldBufferYouTubeScript(m_currentRequest.url());"
new = "    m_xv6BufferedScript.clear();\n    m_xv6BufferYouTubeMainResource = contentType && strstr(contentType, \"text/html\") && xv6IsYouTubeDesktopMainResource(m_currentRequest.url());\n    m_xv6BufferYouTubeScript = !m_xv6BufferYouTubeMainResource && xv6ShouldBufferYouTubeScript(m_currentRequest.url());"
if old in text:
    text = text.replace(old, new, 1)

if "[NDT-YTHTML] buffering" not in text:
    text = text.replace(
        "    if (m_xv6BufferYouTubeScript)\n        fprintf(stderr, \"[NDT-YTJS] buffering url=%.180s\\n\", xv6ShortURL(url));",
        "    if (m_xv6BufferYouTubeMainResource)\n        fprintf(stderr, \"[NDT-YTHTML] buffering url=%.180s\\n\", xv6ShortURL(url));\n    if (m_xv6BufferYouTubeScript)\n        fprintf(stderr, \"[NDT-YTJS] buffering url=%.180s\\n\", xv6ShortURL(url));")

if "ContentSecurityPolicyReportOnly" not in text:
    anchor = "    m_response = ResourceResponse(m_soupMessage.get(), m_sniffedContentType);\n"
    replacement = anchor + '''    if (m_xv6BufferYouTubeMainResource) {
        auto headers = m_response.httpHeaderFields();
        headers.remove(HTTPHeaderName::ContentSecurityPolicy);
        headers.remove(HTTPHeaderName::ContentSecurityPolicyReportOnly);
        headers.remove(HTTPHeaderName::ContentLength);
        m_response.setHTTPHeaderFields(WTFMove(headers));
        m_response.setExpectedContentLength(-1);
        m_response.setMimeType(AtomString { "text/html"_s });
    }

'''
    if anchor not in text:
        raise SystemExit("ports/webkit: NetworkDataTaskSoup response rewrite anchor not found")
    text = text.replace(anchor, replacement, 1)

path.write_text(text)
PY
fi

web_loader_strategy="${src}/Source/WebKit/WebProcess/Network/WebLoaderStrategy.cpp"
if [[ -f "${web_loader_strategy}" ]]; then
    python3 - "${web_loader_strategy}" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
has_legacy_wls_logs = '[WLS] remove:' in text

if has_legacy_wls_logs and "xv6VerboseLoaderLogging" not in text:
    text = text.replace("#include <cstdio>\n", "#include <cstdio>\n#include <cstdlib>\n")
    anchor = "namespace WebKit {\nusing namespace WebCore;\n"
    helper = anchor + r'''
static bool xv6VerboseLoaderLogging()
{
    static bool verbose = getenv("WEBKIT_XV6_VERBOSE_LOADER") != nullptr;
    return verbose;
}

'''
    if anchor not in text:
        raise SystemExit("ports/webkit: WebLoaderStrategy namespace anchor not found")
    text = text.replace(anchor, helper, 1)

old = '    dprintf(2, "[WLS] remove: resourceLoader=%p id=%" PRIu64 " url=%s\\n", resourceLoader, resourceLoader->identifier().toUInt64(), resourceLoader->url().string().utf8().data());'
new = '''    if (xv6VerboseLoaderLogging())
        dprintf(2, "[WLS] remove: resourceLoader=%p id=%" PRIu64 " url=%s\\n", resourceLoader, resourceLoader->identifier().toUInt64(), resourceLoader->url().string().utf8().data());'''
if old in text:
    text = text.replace(old, new, 1)
elif has_legacy_wls_logs and new not in text:
    raise SystemExit("ports/webkit: WebLoaderStrategy remove trace not found")

old = '        dprintf(2, "[WLS] remove: preserving live main resource resourceLoader=%p id=%" PRIu64 " url=%s\\n", resourceLoader, identifier.toUInt64(), resourceLoader->url().string().utf8().data());'
new = '''        if (xv6VerboseLoaderLogging())
            dprintf(2, "[WLS] remove: preserving live main resource resourceLoader=%p id=%" PRIu64 " url=%s\\n", resourceLoader, identifier.toUInt64(), resourceLoader->url().string().utf8().data());'''
if old in text:
    text = text.replace(old, new, 1)
elif has_legacy_wls_logs and new not in text:
    raise SystemExit("ports/webkit: WebLoaderStrategy preserving trace not found")

path.write_text(text)
PY
fi

echo "ports/webkit: applied xv6 WebKitGTK overrides to ${src}"
