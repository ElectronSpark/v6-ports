#ifndef XV6_LIBEVDEV_H
#define XV6_LIBEVDEV_H

#include <linux/input.h>

struct libevdev;

struct libevdev *libevdev_new(void);
void libevdev_free(struct libevdev *dev);
int libevdev_set_fd(struct libevdev *dev, int fd);
const char *libevdev_get_name(const struct libevdev *dev);
int libevdev_get_id_bustype(const struct libevdev *dev);
int libevdev_get_id_vendor(const struct libevdev *dev);
int libevdev_get_id_product(const struct libevdev *dev);
int libevdev_get_id_version(const struct libevdev *dev);
const struct input_absinfo *libevdev_get_abs_info(const struct libevdev *dev,
                                                  unsigned int code);
int libevdev_has_event_code(const struct libevdev *dev, unsigned int type,
                            unsigned int code);
int libevdev_has_event_pending(const struct libevdev *dev);
int libevdev_next_event(struct libevdev *dev, unsigned int flags,
                        struct input_event *ev);
int libevdev_event_code_from_name(unsigned int type, const char *name);

#endif
