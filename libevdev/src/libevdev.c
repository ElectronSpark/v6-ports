#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "libevdev/libevdev.h"

struct libevdev {
    int fd;
    const char *name;
};

int
xv6_libevdev_abi_anchor(void)
{
    return 0;
}

struct libevdev *
libevdev_new(void)
{
    struct libevdev *dev = calloc(1, sizeof(*dev));

    if (dev) {
        dev->fd = -1;
        dev->name = "xv6 virtual input";
    }
    return dev;
}

void
libevdev_free(struct libevdev *dev)
{
    free(dev);
}

int
libevdev_set_fd(struct libevdev *dev, int fd)
{
    if (!dev)
        return -EINVAL;
    dev->fd = fd;
    return 0;
}

const char *
libevdev_get_name(const struct libevdev *dev)
{
    return (dev && dev->name) ? dev->name : "";
}

int
libevdev_get_id_bustype(const struct libevdev *dev)
{
    (void)dev;
    return 0;
}

int
libevdev_get_id_vendor(const struct libevdev *dev)
{
    (void)dev;
    return 0;
}

int
libevdev_get_id_product(const struct libevdev *dev)
{
    (void)dev;
    return 0;
}

int
libevdev_get_id_version(const struct libevdev *dev)
{
    (void)dev;
    return 0;
}

const struct input_absinfo *
libevdev_get_abs_info(const struct libevdev *dev, unsigned int code)
{
    (void)dev;
    (void)code;
    return NULL;
}

int
libevdev_has_event_code(const struct libevdev *dev, unsigned int type,
                        unsigned int code)
{
    (void)dev;
    (void)type;
    (void)code;
    return 0;
}

int
libevdev_has_event_pending(const struct libevdev *dev)
{
    (void)dev;
    return 0;
}

int
libevdev_next_event(struct libevdev *dev, unsigned int flags,
                    struct input_event *ev)
{
    (void)dev;
    (void)flags;
    if (ev)
        memset(ev, 0, sizeof(*ev));
    return -EAGAIN;
}

int
libevdev_event_code_from_name(unsigned int type, const char *name)
{
    (void)type;
    (void)name;
    return -1;
}
