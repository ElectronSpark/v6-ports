#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct _WebKitSettings WebKitSettings;
typedef struct _WebKitWebView WebKitWebView;

typedef enum {
    WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND,
    WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS,
    WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER
} WebKitHardwareAccelerationPolicy;

typedef enum {
    WEBKIT_LOAD_STARTED,
    WEBKIT_LOAD_REDIRECTED,
    WEBKIT_LOAD_COMMITTED,
    WEBKIT_LOAD_FINISHED
} WebKitLoadEvent;

extern WebKitSettings *webkit_settings_new(void);
extern void webkit_settings_set_enable_developer_extras(WebKitSettings *, gboolean);
extern void webkit_settings_set_enable_webgl(WebKitSettings *, gboolean);
extern void webkit_settings_set_hardware_acceleration_policy(WebKitSettings *,
                                                             WebKitHardwareAccelerationPolicy);
extern GtkWidget *webkit_web_view_new(void);
extern WebKitSettings *webkit_web_view_get_settings(WebKitWebView *);
extern void webkit_web_view_load_html(WebKitWebView *, const gchar *, const gchar *);
extern void webkit_web_view_load_uri(WebKitWebView *, const gchar *);

#define WEBKIT_WEB_VIEW(obj) ((WebKitWebView *)(obj))

struct SmokeLoad {
    WebKitWebView *view;
    char *uri;
};

static void phase(const char *message)
{
    fprintf(stderr, "webkitgpusmoke: %s\n", message);
    fflush(stderr);
}

static gboolean quit_cb(gpointer data)
{
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

static void title_changed_cb(GObject *object, GParamSpec *pspec, gpointer data)
{
    gchar *title = NULL;

    (void)pspec;
    (void)data;
    g_object_get(object, "title", &title, NULL);
    if (title) {
        fprintf(stderr, "webkitgpusmoke: title=%s\n", title);
        fflush(stderr);
        g_free(title);
    }
}

static void load_changed_cb(WebKitWebView *view, WebKitLoadEvent event, gpointer data)
{
    (void)view;
    (void)data;
    fprintf(stderr, "webkitgpusmoke: load-event=%d\n", event);
    fflush(stderr);
}

static char *read_text_file(const char *uri)
{
    const char *path = uri;
    if (g_str_has_prefix(uri, "file://"))
        path = uri + 7;

    gchar *contents = NULL;
    gsize length = 0;
    GError *error = NULL;
    if (!g_file_get_contents(path, &contents, &length, &error)) {
        fprintf(stderr, "webkitgpusmoke: failed to read %s: %s\n",
                path, error ? error->message : "unknown error");
        g_clear_error(&error);
        return NULL;
    }

    (void)length;
    return contents;
}

static gboolean start_load_cb(gpointer data)
{
    struct SmokeLoad *load = data;
    char *html = read_text_file(load->uri);
    if (html) {
        webkit_web_view_load_html(load->view, html, "file:///share/webkit/");
        g_free(html);
    } else
        webkit_web_view_load_uri(load->view, load->uri);
    phase("uri load requested");
    g_object_unref(load->view);
    free(load->uri);
    free(load);
    return G_SOURCE_REMOVE;
}

static int env_enabled(const char *name)
{
    const char *value = getenv(name);
    return value && value[0] && strcmp(value, "0") != 0;
}

int main(int argc, char **argv)
{
    const char *uri = argc > 1 ? argv[1] : "file:///share/webkit/gpu-smoke.html";
    int timeout_ms = 0;
    if (argc > 2) {
        timeout_ms = atoi(argv[2]);
    }

    phase("start");
    if (!gtk_init_check(&argc, &argv)) {
        fprintf(stderr, "webkitgpusmoke: gtk_init_check failed\n");
        return 1;
    }
    phase("gtk initialized");

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), 820, 560);
    gtk_window_set_title(GTK_WINDOW(window), "xv6 WebKit GPU API Smoke");
    g_signal_connect(window, "destroy", G_CALLBACK(quit_cb), NULL);
    phase("window created");

    GtkWidget *view = webkit_web_view_new();
    phase("web view created");
    WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(view));
    phase("settings acquired");
    webkit_settings_set_enable_developer_extras(settings, TRUE);
    webkit_settings_set_enable_webgl(settings, TRUE);
    webkit_settings_set_hardware_acceleration_policy(
        settings,
        env_enabled("WEBKIT_XV6_FORCE_COMPOSITING_MODE") ?
            WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS :
            WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND);
    phase("settings applied");
    g_signal_connect(view, "notify::title", G_CALLBACK(title_changed_cb), NULL);
    g_signal_connect(view, "load-changed", G_CALLBACK(load_changed_cb), NULL);
    gtk_container_add(GTK_CONTAINER(window), view);
    gtk_widget_show_all(window);
    phase("window shown");
    struct SmokeLoad *load = calloc(1, sizeof(*load));
    load->view = WEBKIT_WEB_VIEW(g_object_ref(view));
    load->uri = strdup(uri);
    g_timeout_add(1500, start_load_cb, load);

    if (timeout_ms > 0)
        g_timeout_add(timeout_ms, quit_cb, NULL);
    gtk_main();
    fprintf(stderr, "webkitgpusmoke: complete uri=%s timeout_ms=%d\n", uri, timeout_ms);
    return 0;
}
