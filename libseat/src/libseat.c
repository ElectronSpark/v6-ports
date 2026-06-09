#include "libseat.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct libseat {
    const struct libseat_seat_listener *listener;
    void *data;
    int pipefd[2];
    int enabled;
    int next_device_id;
};

static libseat_log_func log_handler;
static enum libseat_log_level log_level = LIBSEAT_LOG_LEVEL_ERROR;

void
libseat_set_log_handler(libseat_log_func handler)
{
    log_handler = handler;
    (void)log_handler;
}

void
libseat_set_log_level(enum libseat_log_level level)
{
    log_level = level;
    (void)log_level;
}

struct libseat *
libseat_open_seat(const struct libseat_seat_listener *listener, void *data)
{
    struct libseat *seat = calloc(1, sizeof(*seat));
    if (!seat)
        return NULL;
    seat->listener = listener;
    seat->data = data;
    seat->pipefd[0] = -1;
    seat->pipefd[1] = -1;
    seat->next_device_id = 1;
    if (pipe(seat->pipefd) != 0) {
        free(seat);
        return NULL;
    }
    fcntl(seat->pipefd[0], F_SETFL, fcntl(seat->pipefd[0], F_GETFL, 0) | O_NONBLOCK);
    fcntl(seat->pipefd[1], F_SETFL, fcntl(seat->pipefd[1], F_GETFL, 0) | O_NONBLOCK);
    return seat;
}

int
libseat_disable_seat(struct libseat *seat)
{
    if (!seat)
        return -1;
    seat->enabled = 0;
    return 0;
}

int
libseat_close_seat(struct libseat *seat)
{
    if (!seat)
        return -1;
    if (seat->pipefd[0] >= 0)
        close(seat->pipefd[0]);
    if (seat->pipefd[1] >= 0)
        close(seat->pipefd[1]);
    free(seat);
    return 0;
}

int
libseat_get_fd(struct libseat *seat)
{
    return seat ? seat->pipefd[0] : -1;
}

int
libseat_dispatch(struct libseat *seat, int timeout)
{
    char buf[32];
    struct pollfd pfd;

    if (!seat)
        return -1;
    if (timeout > 0) {
        pfd.fd = seat->pipefd[0];
        pfd.events = POLLIN;
        poll(&pfd, 1, timeout);
    }
    while (read(seat->pipefd[0], buf, sizeof(buf)) > 0)
        ;
    if (!seat->enabled) {
        seat->enabled = 1;
        if (seat->listener && seat->listener->enable_seat)
            seat->listener->enable_seat(seat, seat->data);
    }
    return 0;
}

int
libseat_open_device(struct libseat *seat, const char *path, int *fd)
{
    int opened;
    if (!seat || !path || !fd)
        return -1;
    opened = open(path, O_RDWR | O_CLOEXEC);
    if (opened < 0)
        opened = open(path, O_RDONLY | O_CLOEXEC);
    if (opened < 0)
        return -1;
    *fd = opened;
    return seat->next_device_id++;
}

int
libseat_close_device(struct libseat *seat, int device_id)
{
    (void)seat;
    (void)device_id;
    return 0;
}

int
libseat_switch_session(struct libseat *seat, int session)
{
    (void)seat;
    (void)session;
    errno = ENOSYS;
    return -1;
}
