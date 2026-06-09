#ifndef XV6_LIBSEAT_H
#define XV6_LIBSEAT_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

struct libseat;

enum libseat_log_level {
    LIBSEAT_LOG_LEVEL_ERROR = 0,
    LIBSEAT_LOG_LEVEL_INFO = 1,
    LIBSEAT_LOG_LEVEL_DEBUG = 2,
};

struct libseat_seat_listener {
    void (*enable_seat)(struct libseat *seat, void *data);
    void (*disable_seat)(struct libseat *seat, void *data);
};

typedef void (*libseat_log_func)(enum libseat_log_level level,
                                 const char *fmt,
                                 va_list args);

struct libseat *libseat_open_seat(const struct libseat_seat_listener *listener,
                                  void *data);
int libseat_disable_seat(struct libseat *seat);
int libseat_close_seat(struct libseat *seat);
int libseat_get_fd(struct libseat *seat);
int libseat_dispatch(struct libseat *seat, int timeout);
int libseat_open_device(struct libseat *seat, const char *path, int *fd);
int libseat_close_device(struct libseat *seat, int device_id);
int libseat_switch_session(struct libseat *seat, int session);
void libseat_set_log_handler(libseat_log_func handler);
void libseat_set_log_level(enum libseat_log_level level);

#ifdef __cplusplus
}
#endif

#endif
