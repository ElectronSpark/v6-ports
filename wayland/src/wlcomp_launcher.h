#ifndef WLCOMP_LAUNCHER_H
#define WLCOMP_LAUNCHER_H

#include <stdint.h>
#include <sys/types.h>

struct wlcomp_launcher_ops {
    uint32_t (*get_time_ms)(void);
    int (*cmdline_flag_enabled)(const char *key);
    int (*cmdline_int_value)(const char *key, int fallback);
    int (*virgl_available)(void);
    void (*destroy_surfaces_for_pid)(pid_t pid);
};

void wlcomp_launcher_init(const struct wlcomp_launcher_ops *ops);
void wlcomp_launcher_launch_args(const char *path, const char *name,
                                 const char *arg1, const char *arg2,
                                 const char *arg3);
void wlcomp_launcher_launch(const char *path, const char *name, const char *arg);
void wlcomp_launcher_launch_noarg(const char *path, const char *name);
void wlcomp_launcher_reap(void);
void wlcomp_launcher_terminate_pid(pid_t pid);
void wlcomp_launcher_remember_reap(pid_t pid);
void wlcomp_launcher_signal_process_group(pid_t pid, int sig);
const char *wlcomp_path_basename(const char *path);
int wlcomp_path_has_suffix(const char *path, const char *suffix);
int wlcomp_path_is_html(const char *path);
int wlcomp_path_is_text(const char *path);

#endif
