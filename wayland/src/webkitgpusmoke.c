#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct _WebKitSettings WebKitSettings;
typedef struct _WebKitWebView WebKitWebView;

typedef enum {
    WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND,
    WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS,
    WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER
} WebKitHardwareAccelerationPolicy;

extern WebKitSettings *webkit_settings_new(void);
extern void webkit_settings_set_enable_developer_extras(WebKitSettings *, gboolean);
extern void webkit_settings_set_enable_webgl(WebKitSettings *, gboolean);
extern void webkit_settings_set_hardware_acceleration_policy(WebKitSettings *,
                                                             WebKitHardwareAccelerationPolicy);
extern GtkWidget *webkit_web_view_new_with_settings(WebKitSettings *);
extern void webkit_web_view_load_uri(WebKitWebView *, const gchar *);

#define WEBKIT_WEB_VIEW(obj) ((WebKitWebView *)(obj))

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

int main(int argc, char **argv)
{
    const char *uri = argc > 1 ? argv[1] : "file:///share/webkit/gpu-smoke.html";
    int timeout_ms = 15000;
    if (argc > 2) {
        timeout_ms = atoi(argv[2]);
        if (timeout_ms <= 0)
            timeout_ms = 15000;
    }

    if (!gtk_init_check(&argc, &argv)) {
        fprintf(stderr, "webkitgpusmoke: gtk_init_check failed\n");
        return 1;
    }

    WebKitSettings *settings = webkit_settings_new();
    webkit_settings_set_enable_developer_extras(settings, TRUE);
    webkit_settings_set_enable_webgl(settings, TRUE);
    webkit_settings_set_hardware_acceleration_policy(
        settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), 820, 560);
    gtk_window_set_title(GTK_WINDOW(window), "xv6 WebKit GPU API Smoke");
    g_signal_connect(window, "destroy", G_CALLBACK(quit_cb), NULL);

    GtkWidget *view = webkit_web_view_new_with_settings(settings);
    g_signal_connect(view, "notify::title", G_CALLBACK(title_changed_cb), NULL);
    gtk_container_add(GTK_CONTAINER(window), view);
    gtk_widget_show_all(window);
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(view), uri);

    g_timeout_add(timeout_ms, quit_cb, NULL);
    gtk_main();
    fprintf(stderr, "webkitgpusmoke: complete uri=%s timeout_ms=%d\n", uri, timeout_ms);
    return 0;
}
