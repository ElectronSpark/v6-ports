#include "libudev.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define XV6_UDEV_PRIMARY_NODE "/dev/dri/card0"
#define XV6_UDEV_RENDER_NODE "/dev/dri/renderD128"
#define XV6_UDEV_KEYBOARD_NODE "/dev/input/event0"
#define XV6_UDEV_POINTER_NODE "/dev/input/event1"
#define XV6_UDEV_PRIMARY_SYSPATH "/sys/dev/char/226:0"
#define XV6_UDEV_RENDER_SYSPATH "/sys/dev/char/226:128"
#define XV6_UDEV_KEYBOARD_SYSPATH "/sys/dev/char/13:64"
#define XV6_UDEV_POINTER_SYSPATH "/sys/dev/char/13:65"

static const char *const xv6_subsystem_syspaths[] = {
    "/sys/class/drm",
    "/sys/class/input",
    "/sys/class/block",
    "/sys/bus/pci",
    "/sys/bus/virtio",
    NULL,
};

struct udev {
    int refcount;
    void *userdata;
};

struct udev_list_entry {
    char *name;
    char *value;
    struct udev_list_entry *next;
};

struct udev_device {
    int refcount;
    struct udev *udev;
    struct udev_device *parent;
    char *syspath;
    char *sysname;
    char *sysnum;
    char *devnode;
    char *subsystem;
    char *devtype;
    char *action;
    dev_t devnum;
    struct udev_list_entry *properties;
    struct udev_list_entry *sysattrs;
    struct udev_list_entry *tags;
};

struct udev_enumerate {
    int refcount;
    struct udev *udev;
    char *match_subsystem;
    struct udev_list_entry *match_tags;
    struct udev_list_entry *devices;
};

struct udev_monitor {
    int refcount;
    struct udev *udev;
    char *subsystem;
    char *devtype;
    int pipefd[2];
};

static char *
xstrdup(const char *s)
{
    if (!s)
        return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy)
        memcpy(copy, s, len);
    return copy;
}

static void
list_free(struct udev_list_entry *entry)
{
    while (entry) {
        struct udev_list_entry *next = entry->next;
        free(entry->name);
        free(entry->value);
        free(entry);
        entry = next;
    }
}

static int
list_add(struct udev_list_entry **head, const char *name, const char *value)
{
    struct udev_list_entry *entry = calloc(1, sizeof(*entry));
    if (!entry)
        return -ENOMEM;
    entry->name = xstrdup(name);
    entry->value = xstrdup(value);
    if (!entry->name || (value && !entry->value)) {
        list_free(entry);
        return -ENOMEM;
    }
    if (!*head) {
        *head = entry;
    } else {
        struct udev_list_entry *tail = *head;
        while (tail->next)
            tail = tail->next;
        tail->next = entry;
    }
    return 0;
}

static bool
list_contains_name(struct udev_list_entry *head, const char *name)
{
    for (; head; head = head->next) {
        if (head->name && name && strcmp(head->name, name) == 0)
            return true;
    }
    return false;
}

static const char *
node_for_syspath(const char *syspath)
{
    if (!syspath)
        return NULL;
    if (strcmp(syspath, XV6_UDEV_PRIMARY_SYSPATH) == 0 ||
        strcmp(syspath, "/sys/class/drm/card0") == 0)
        return XV6_UDEV_PRIMARY_NODE;
    if (strcmp(syspath, XV6_UDEV_RENDER_SYSPATH) == 0 ||
        strcmp(syspath, "/sys/class/drm/renderD128") == 0)
        return XV6_UDEV_RENDER_NODE;
    if (strcmp(syspath, XV6_UDEV_KEYBOARD_SYSPATH) == 0 ||
        strcmp(syspath, "/sys/class/input/event0") == 0)
        return XV6_UDEV_KEYBOARD_NODE;
    if (strcmp(syspath, XV6_UDEV_POINTER_SYSPATH) == 0 ||
        strcmp(syspath, "/sys/class/input/event1") == 0)
        return XV6_UDEV_POINTER_NODE;
    return NULL;
}

static const char *
syspath_for_node(const char *node)
{
    if (!node)
        return NULL;
    if (strcmp(node, XV6_UDEV_PRIMARY_NODE) == 0)
        return XV6_UDEV_PRIMARY_SYSPATH;
    if (strcmp(node, XV6_UDEV_RENDER_NODE) == 0)
        return XV6_UDEV_RENDER_SYSPATH;
    if (strcmp(node, XV6_UDEV_KEYBOARD_NODE) == 0)
        return XV6_UDEV_KEYBOARD_SYSPATH;
    if (strcmp(node, XV6_UDEV_POINTER_NODE) == 0)
        return XV6_UDEV_POINTER_SYSPATH;
    return NULL;
}

static const char *
subsystem_name_for_syspath(const char *syspath)
{
    const char *slash;

    if (!syspath)
        return NULL;
    slash = strrchr(syspath, '/');
    if (!slash || !slash[1])
        return NULL;
    return slash + 1;
}

static bool
subsystem_matches_filter(const char *syspath, const char *match_subsystem)
{
    const char *name = subsystem_name_for_syspath(syspath);

    if (!match_subsystem)
        return true;
    return name && strcmp(name, match_subsystem) == 0;
}

static bool
known_subsystem_syspath(const char *syspath)
{
    for (int i = 0; xv6_subsystem_syspaths[i]; i++) {
        if (strcmp(syspath, xv6_subsystem_syspaths[i]) == 0)
            return true;
    }
    return false;
}

static struct udev_device *
input_parent_new(struct udev *udev, bool keyboard)
{
    struct udev_device *dev;
    const char *input_num = keyboard ? "input0" : "input1";
    const char *name = keyboard ? "xv6 PS/2 Keyboard" : "xv6 Virtio/PS2 Pointer";
    const char *phys = keyboard ? "xv6/input0" : "xv6/input1";
    const char *syspath = keyboard ? "/sys/devices/virtual/input/input0" :
                                     "/sys/devices/virtual/input/input1";

    dev = calloc(1, sizeof(*dev));
    if (!dev)
        return NULL;

    dev->refcount = 1;
    dev->udev = udev_ref(udev);
    dev->syspath = xstrdup(syspath);
    dev->sysname = xstrdup(input_num);
    dev->subsystem = xstrdup("input");
    dev->action = xstrdup("add");
    list_add(&dev->properties, "SUBSYSTEM", "input");
    list_add(&dev->properties, "ID_INPUT", "1");
    list_add(&dev->properties, "ID_BUS", "xv6");
    if (keyboard)
        list_add(&dev->properties, "ID_INPUT_KEYBOARD", "1");
    else
        list_add(&dev->properties, "ID_INPUT_MOUSE", "1");
    list_add(&dev->sysattrs, "name", name);
    list_add(&dev->sysattrs, "phys", phys);
    list_add(&dev->sysattrs, "id/bustype", "0006");
    list_add(&dev->sysattrs, "id/vendor", "5856");
    list_add(&dev->sysattrs, "id/product", keyboard ? "0001" : "0002");
    list_add(&dev->sysattrs, "id/version", "0001");
    if (keyboard) {
        list_add(&dev->sysattrs, "capabilities/ev", "120003");
        list_add(&dev->sysattrs, "capabilities/key",
                 "ffffffffffffffff ffffffffffffffffffffffff 0 0");
    } else {
        list_add(&dev->sysattrs, "capabilities/ev", "b");
        list_add(&dev->sysattrs, "capabilities/key", "70000 0 0 0 0");
        list_add(&dev->sysattrs, "capabilities/rel", "103");
        list_add(&dev->sysattrs, "capabilities/abs", "3");
        list_add(&dev->sysattrs, "capabilities/prop", "1");
    }
    if (!dev->syspath || !dev->sysname || !dev->subsystem || !dev->action) {
        udev_device_unref(dev);
        return NULL;
    }
    return dev;
}

static struct udev_device *
device_new(struct udev *udev, const char *node)
{
    struct stat st;
    char buf[64];
    const char *syspath = syspath_for_node(node);
    const char *sysname;
    bool primary;
    bool drm;
    bool keyboard;
    bool pointer;

    if (!udev || !node || !syspath || stat(node, &st) != 0)
        return NULL;
    if (!S_ISCHR(st.st_mode))
        return NULL;

    struct udev_device *dev = calloc(1, sizeof(*dev));
    if (!dev)
        return NULL;

    dev->refcount = 1;
    dev->udev = udev_ref(udev);
    dev->syspath = xstrdup(syspath);
    dev->devnode = xstrdup(node);
    dev->action = xstrdup("add");
    dev->devnum = st.st_rdev;
    sysname = strrchr(node, '/');
    dev->sysname = xstrdup(sysname ? sysname + 1 : node);
    dev->sysnum = xstrdup(strncmp(dev->sysname ? dev->sysname : "", "event", 5) == 0
                              ? (dev->sysname + 5)
                              : (minor(st.st_rdev) >= 128 ? "128" : "0"));
    primary = strcmp(node, XV6_UDEV_PRIMARY_NODE) == 0;
    drm = strncmp(node, "/dev/dri/", 9) == 0;
    keyboard = strcmp(node, XV6_UDEV_KEYBOARD_NODE) == 0;
    pointer = strcmp(node, XV6_UDEV_POINTER_NODE) == 0;
    dev->subsystem = xstrdup(drm ? "drm" : "input");
    dev->devtype = xstrdup(drm ? "drm_minor" : "input_device");
    if (!drm)
        dev->parent = input_parent_new(udev, keyboard);

    if (!dev->syspath || !dev->devnode || !dev->subsystem ||
        !dev->devtype || !dev->action || !dev->sysname || !dev->sysnum ||
        (!drm && !dev->parent)) {
        udev_device_unref(dev);
        return NULL;
    }

    snprintf(buf, sizeof(buf), "%u", major(st.st_rdev));
    list_add(&dev->properties, "MAJOR", buf);
    snprintf(buf, sizeof(buf), "%u", minor(st.st_rdev));
    list_add(&dev->properties, "MINOR", buf);
    list_add(&dev->properties, "DEVNAME", node);
    list_add(&dev->properties, "HOTPLUG", "1");
    list_add(&dev->properties, "ID_SEAT", "seat0");
    list_add(&dev->properties, "SUBSYSTEM", drm ? "drm" : "input");
    list_add(&dev->properties, "DEVTYPE", drm ? "drm_minor" : "input_device");
    list_add(&dev->tags, "seat", "1");
    list_add(&dev->tags, "uaccess", "1");
    if (drm) {
        list_add(&dev->properties, "ID_FOR_SEAT", "drm-virtio_gpu-card0");
        list_add(&dev->properties, "ID_PATH", "virtio-gpu");
    } else {
        list_add(&dev->properties, "ID_INPUT", "1");
        list_add(&dev->properties, "ID_BUS", "xv6");
        list_add(&dev->properties, "ID_PATH", "xv6-input");
        list_add(&dev->properties, "ID_FOR_SEAT",
                 keyboard ? "input-xv6-keyboard" : "input-xv6-pointer");
        if (keyboard) {
            list_add(&dev->properties, "ID_INPUT_KEYBOARD", "1");
            list_add(&dev->sysattrs, "name", "xv6 PS/2 Keyboard");
            list_add(&dev->sysattrs, "phys", "xv6/input0");
            list_add(&dev->sysattrs, "capabilities/ev", "120003");
            list_add(&dev->sysattrs, "capabilities/key",
                     "ffffffffffffffff ffffffffffffffffffffffff 0 0");
        } else if (pointer) {
            list_add(&dev->properties, "ID_INPUT_MOUSE", "1");
            list_add(&dev->sysattrs, "name", "xv6 Virtio/PS2 Pointer");
            list_add(&dev->sysattrs, "phys", "xv6/input1");
            list_add(&dev->sysattrs, "capabilities/ev", "b");
            list_add(&dev->sysattrs, "capabilities/key", "70000 0 0 0 0");
            list_add(&dev->sysattrs, "capabilities/rel", "103");
            list_add(&dev->sysattrs, "capabilities/abs", "3");
            list_add(&dev->sysattrs, "capabilities/prop", "1");
        }
        list_add(&dev->sysattrs, "id/bustype", "0006");
        list_add(&dev->sysattrs, "id/vendor", "5856");
        list_add(&dev->sysattrs, "id/product", keyboard ? "0001" : "0002");
        list_add(&dev->sysattrs, "id/version", "0001");
    }
    if (primary) {
        list_add(&dev->tags, "master-of-seat", "1");
        list_add(&dev->properties, "ID_TAG_MASTER_OF_SEAT", "1");
        list_add(&dev->properties, "TAGS", ":seat:uaccess:master-of-seat:");
        list_add(&dev->properties, "CURRENT_TAGS", ":seat:uaccess:master-of-seat:");
    } else {
        list_add(&dev->properties, "TAGS", ":seat:uaccess:");
        list_add(&dev->properties, "CURRENT_TAGS", ":seat:uaccess:");
    }
    snprintf(buf, sizeof(buf), "%u:%u", major(st.st_rdev), minor(st.st_rdev));
    list_add(&dev->sysattrs, "dev", buf);

    return dev;
}

static struct udev_device *
subsystem_device_new(struct udev *udev, const char *syspath)
{
    const char *name = subsystem_name_for_syspath(syspath);
    struct udev_device *dev;

    if (!udev || !syspath || !name || !known_subsystem_syspath(syspath))
        return NULL;

    dev = calloc(1, sizeof(*dev));
    if (!dev)
        return NULL;

    dev->refcount = 1;
    dev->udev = udev_ref(udev);
    dev->syspath = xstrdup(syspath);
    dev->sysname = xstrdup(name);
    dev->subsystem = xstrdup("subsystem");
    dev->devtype = xstrdup("subsystem");
    dev->action = xstrdup("add");

    if (!dev->syspath || !dev->sysname || !dev->subsystem ||
        !dev->devtype || !dev->action) {
        udev_device_unref(dev);
        return NULL;
    }

    list_add(&dev->properties, "SUBSYSTEM", name);
    list_add(&dev->properties, "DEVTYPE", "subsystem");
    return dev;
}

static bool
device_matches_tags(struct udev_device *dev, struct udev_list_entry *match_tags)
{
    struct udev_list_entry *entry;
    if (!match_tags)
        return true;
    if (!dev)
        return false;
    for (entry = match_tags; entry; entry = entry->next) {
        if (list_contains_name(dev->tags, entry->name))
            return true;
    }
    return false;
}

static bool
node_matches_tags(struct udev *udev, const char *node,
                  struct udev_list_entry *match_tags)
{
    struct udev_device *dev;
    bool matched;
    if (!match_tags)
        return true;
    dev = device_new(udev, node);
    matched = device_matches_tags(dev, match_tags);
    udev_device_unref(dev);
    return matched;
}

struct udev *
udev_new(void)
{
    struct udev *udev = calloc(1, sizeof(*udev));
    if (udev)
        udev->refcount = 1;
    return udev;
}

struct udev *
udev_ref(struct udev *udev)
{
    if (udev)
        udev->refcount++;
    return udev;
}

struct udev *
udev_unref(struct udev *udev)
{
    if (udev && --udev->refcount == 0)
        free(udev);
    return NULL;
}

void *
udev_get_userdata(struct udev *udev)
{
    return udev ? udev->userdata : NULL;
}

void
udev_set_userdata(struct udev *udev, void *userdata)
{
    if (udev)
        udev->userdata = userdata;
}

struct udev_enumerate *
udev_enumerate_new(struct udev *udev)
{
    if (!udev)
        return NULL;
    struct udev_enumerate *e = calloc(1, sizeof(*e));
    if (!e)
        return NULL;
    e->refcount = 1;
    e->udev = udev_ref(udev);
    return e;
}

struct udev_enumerate *
udev_enumerate_ref(struct udev_enumerate *e)
{
    if (e)
        e->refcount++;
    return e;
}

struct udev_enumerate *
udev_enumerate_unref(struct udev_enumerate *e)
{
    if (e && --e->refcount == 0) {
        udev_unref(e->udev);
        free(e->match_subsystem);
        list_free(e->match_tags);
        list_free(e->devices);
        free(e);
    }
    return NULL;
}

int
udev_enumerate_add_match_subsystem(struct udev_enumerate *e,
                                   const char *subsystem)
{
    if (!e)
        return -EINVAL;
    free(e->match_subsystem);
    e->match_subsystem = xstrdup(subsystem);
    return subsystem && !e->match_subsystem ? -ENOMEM : 0;
}

int
udev_enumerate_add_match_property(struct udev_enumerate *e,
                                  const char *property,
                                  const char *value)
{
    (void)e;
    (void)property;
    (void)value;
    return 0;
}

int
udev_enumerate_add_match_is_initialized(struct udev_enumerate *e)
{
    return e ? 0 : -EINVAL;
}

int
udev_enumerate_add_match_sysattr(struct udev_enumerate *e,
                                 const char *sysattr,
                                 const char *value)
{
    (void)e;
    (void)sysattr;
    (void)value;
    return 0;
}

int
udev_enumerate_add_match_sysname(struct udev_enumerate *e, const char *sysname)
{
    (void)e;
    (void)sysname;
    return 0;
}

int
udev_enumerate_add_match_tag(struct udev_enumerate *e, const char *tag)
{
    if (!e || !tag)
        return -EINVAL;
    if (list_contains_name(e->match_tags, tag))
        return 0;
    return list_add(&e->match_tags, tag, NULL);
}

int
udev_enumerate_add_nomatch_subsystem(struct udev_enumerate *e,
                                     const char *subsystem)
{
    (void)e;
    (void)subsystem;
    return 0;
}

int
udev_enumerate_add_nomatch_sysattr(struct udev_enumerate *e,
                                   const char *sysattr,
                                   const char *value)
{
    (void)e;
    (void)sysattr;
    (void)value;
    return 0;
}

int
udev_enumerate_add_syspath(struct udev_enumerate *e, const char *syspath)
{
    const char *node;
    if (!e || !syspath)
        return -EINVAL;
    node = node_for_syspath(syspath);
    if (!node)
        return 0;
    if (!node_matches_tags(e->udev, node, e->match_tags))
        return 0;
    return list_add(&e->devices, syspath, NULL);
}

int
udev_enumerate_scan_devices(struct udev_enumerate *e)
{
    if (!e)
        return -EINVAL;
    list_free(e->devices);
    e->devices = NULL;
    if (e->match_subsystem &&
        strcmp(e->match_subsystem, "drm") != 0 &&
        strcmp(e->match_subsystem, "input") != 0)
        return 0;
    if ((!e->match_subsystem || strcmp(e->match_subsystem, "drm") == 0) &&
        access(XV6_UDEV_PRIMARY_NODE, F_OK) == 0 &&
        node_matches_tags(e->udev, XV6_UDEV_PRIMARY_NODE, e->match_tags))
        list_add(&e->devices, XV6_UDEV_PRIMARY_SYSPATH, NULL);
    if ((!e->match_subsystem || strcmp(e->match_subsystem, "drm") == 0) &&
        access(XV6_UDEV_RENDER_NODE, F_OK) == 0 &&
        node_matches_tags(e->udev, XV6_UDEV_RENDER_NODE, e->match_tags))
        list_add(&e->devices, XV6_UDEV_RENDER_SYSPATH, NULL);
    if ((!e->match_subsystem || strcmp(e->match_subsystem, "input") == 0) &&
        access(XV6_UDEV_KEYBOARD_NODE, F_OK) == 0 &&
        node_matches_tags(e->udev, XV6_UDEV_KEYBOARD_NODE, e->match_tags))
        list_add(&e->devices, XV6_UDEV_KEYBOARD_SYSPATH, NULL);
    if ((!e->match_subsystem || strcmp(e->match_subsystem, "input") == 0) &&
        access(XV6_UDEV_POINTER_NODE, F_OK) == 0 &&
        node_matches_tags(e->udev, XV6_UDEV_POINTER_NODE, e->match_tags))
        list_add(&e->devices, XV6_UDEV_POINTER_SYSPATH, NULL);
    return 0;
}

int
udev_enumerate_scan_subsystems(struct udev_enumerate *e)
{
    if (!e)
        return -EINVAL;
    list_free(e->devices);
    e->devices = NULL;
    for (int i = 0; xv6_subsystem_syspaths[i]; i++) {
        if (subsystem_matches_filter(xv6_subsystem_syspaths[i],
                                     e->match_subsystem))
            list_add(&e->devices, xv6_subsystem_syspaths[i], NULL);
    }
    return 0;
}

struct udev *
udev_enumerate_get_udev(struct udev_enumerate *e)
{
    return e ? e->udev : NULL;
}

struct udev_list_entry *
udev_enumerate_get_list_entry(struct udev_enumerate *e)
{
    return e ? e->devices : NULL;
}

struct udev_list_entry *
udev_list_entry_get_next(struct udev_list_entry *entry)
{
    return entry ? entry->next : NULL;
}

struct udev_list_entry *
udev_list_entry_get_by_name(struct udev_list_entry *entry, const char *name)
{
    for (; entry; entry = entry->next) {
        if (entry->name && name && strcmp(entry->name, name) == 0)
            return entry;
    }
    return NULL;
}

const char *
udev_list_entry_get_name(struct udev_list_entry *entry)
{
    return entry ? entry->name : NULL;
}

const char *
udev_list_entry_get_value(struct udev_list_entry *entry)
{
    return entry ? entry->value : NULL;
}

struct udev_device *
udev_device_ref(struct udev_device *dev)
{
    if (dev)
        dev->refcount++;
    return dev;
}

struct udev_device *
udev_device_unref(struct udev_device *dev)
{
    if (dev && --dev->refcount == 0) {
        udev_unref(dev->udev);
        free(dev->syspath);
        free(dev->sysname);
        free(dev->sysnum);
        free(dev->devnode);
        free(dev->subsystem);
        free(dev->devtype);
        free(dev->action);
        list_free(dev->properties);
        list_free(dev->sysattrs);
        list_free(dev->tags);
        udev_device_unref(dev->parent);
        free(dev);
    }
    return NULL;
}

struct udev_device *
udev_device_new_from_syspath(struct udev *udev, const char *syspath)
{
    struct udev_device *dev = device_new(udev, node_for_syspath(syspath));
    if (dev)
        return dev;
    return subsystem_device_new(udev, syspath);
}

struct udev_device *
udev_device_new_from_devnum(struct udev *udev, char type, dev_t devnum)
{
    struct stat st;
    if (type != 'c')
        return NULL;
    if (stat(XV6_UDEV_PRIMARY_NODE, &st) == 0 && st.st_rdev == devnum)
        return device_new(udev, XV6_UDEV_PRIMARY_NODE);
    if (stat(XV6_UDEV_RENDER_NODE, &st) == 0 && st.st_rdev == devnum)
        return device_new(udev, XV6_UDEV_RENDER_NODE);
    if (stat(XV6_UDEV_KEYBOARD_NODE, &st) == 0 && st.st_rdev == devnum)
        return device_new(udev, XV6_UDEV_KEYBOARD_NODE);
    if (stat(XV6_UDEV_POINTER_NODE, &st) == 0 && st.st_rdev == devnum)
        return device_new(udev, XV6_UDEV_POINTER_NODE);
    return NULL;
}

struct udev_device *
udev_device_new_from_subsystem_sysname(struct udev *udev,
                                       const char *subsystem,
                                       const char *sysname)
{
    if (!subsystem || !sysname)
        return NULL;
    if (strcmp(subsystem, "drm") == 0) {
        if (strcmp(sysname, "card0") == 0)
            return device_new(udev, XV6_UDEV_PRIMARY_NODE);
        if (strcmp(sysname, "renderD128") == 0)
            return device_new(udev, XV6_UDEV_RENDER_NODE);
    } else if (strcmp(subsystem, "input") == 0) {
        if (strcmp(sysname, "event0") == 0)
            return device_new(udev, XV6_UDEV_KEYBOARD_NODE);
        if (strcmp(sysname, "event1") == 0)
            return device_new(udev, XV6_UDEV_POINTER_NODE);
    }
    return NULL;
}

struct udev *
udev_device_get_udev(struct udev_device *dev)
{
    return dev ? dev->udev : NULL;
}

const char *udev_device_get_syspath(struct udev_device *dev) { return dev ? dev->syspath : NULL; }
const char *udev_device_get_sysname(struct udev_device *dev) { return dev ? dev->sysname : NULL; }
const char *udev_device_get_devnode(struct udev_device *dev) { return dev ? dev->devnode : NULL; }
const char *udev_device_get_subsystem(struct udev_device *dev) { return dev ? dev->subsystem : NULL; }
const char *udev_device_get_devtype(struct udev_device *dev) { return dev ? dev->devtype : NULL; }
const char *udev_device_get_action(struct udev_device *dev) { return dev ? dev->action : NULL; }
dev_t udev_device_get_devnum(struct udev_device *dev) { return dev ? dev->devnum : 0; }
const char *udev_device_get_driver(struct udev_device *dev)
{
    if (!dev)
        return NULL;
    if (dev->subsystem && strcmp(dev->subsystem, "input") == 0)
        return "xv6-input";
    return "virtio_gpu";
}
int udev_device_get_is_initialized(struct udev_device *dev) { return dev ? 1 : 0; }
struct udev_device *udev_device_get_parent(struct udev_device *dev) { return dev ? dev->parent : NULL; }

struct udev_device *
udev_device_get_parent_with_subsystem_devtype(struct udev_device *dev,
                                              const char *subsystem,
                                              const char *devtype)
{
    if (!dev || !dev->parent)
        return NULL;
    if (subsystem && (!dev->parent->subsystem ||
        strcmp(dev->parent->subsystem, subsystem) != 0))
        return NULL;
    if (devtype && (!dev->parent->devtype ||
        strcmp(dev->parent->devtype, devtype) != 0))
        return NULL;
    return dev->parent;
}

struct udev_list_entry *
udev_device_get_properties_list_entry(struct udev_device *dev)
{
    return dev ? dev->properties : NULL;
}

struct udev_list_entry *
udev_device_get_sysattr_list_entry(struct udev_device *dev)
{
    return dev ? dev->sysattrs : NULL;
}

struct udev_list_entry *
udev_device_get_devlinks_list_entry(struct udev_device *dev)
{
    (void)dev;
    return NULL;
}

struct udev_list_entry *
udev_device_get_tags_list_entry(struct udev_device *dev)
{
    return dev ? dev->tags : NULL;
}

struct udev_list_entry *
udev_device_get_current_tags_list_entry(struct udev_device *dev)
{
    return dev ? dev->tags : NULL;
}

const char *
udev_device_get_property_value(struct udev_device *dev, const char *key)
{
    struct udev_list_entry *entry;
    if (!dev)
        return NULL;
    entry = udev_list_entry_get_by_name(dev->properties, key);
    return entry ? entry->value : NULL;
}

const char *
udev_device_get_sysattr_value(struct udev_device *dev, const char *sysattr)
{
    struct udev_list_entry *entry;
    if (!dev)
        return NULL;
    entry = udev_list_entry_get_by_name(dev->sysattrs, sysattr);
    return entry ? entry->value : NULL;
}

const char *
udev_device_get_sysnum(struct udev_device *dev)
{
    return dev ? dev->sysnum : NULL;
}

unsigned long long
udev_device_get_seqnum(struct udev_device *dev)
{
    return dev ? 1 : 0;
}

unsigned long long
udev_device_get_usec_since_initialized(struct udev_device *dev)
{
    (void)dev;
    return 0;
}

int
udev_device_set_sysattr_value(struct udev_device *dev,
                              const char *sysattr,
                              char *value)
{
    (void)dev;
    (void)sysattr;
    (void)value;
    errno = EROFS;
    return -EROFS;
}

int
udev_device_has_tag(struct udev_device *dev, const char *tag)
{
    return dev && tag && list_contains_name(dev->tags, tag);
}

int
udev_device_has_current_tag(struct udev_device *dev, const char *tag)
{
    return udev_device_has_tag(dev, tag);
}

struct udev_monitor *
udev_monitor_new_from_netlink(struct udev *udev, const char *name)
{
    if (!udev || !name)
        return NULL;
    struct udev_monitor *mon = calloc(1, sizeof(*mon));
    if (!mon)
        return NULL;
    mon->refcount = 1;
    mon->udev = udev_ref(udev);
    mon->pipefd[0] = -1;
    mon->pipefd[1] = -1;
    if (pipe(mon->pipefd) != 0) {
        udev_monitor_unref(mon);
        return NULL;
    }
    fcntl(mon->pipefd[0], F_SETFL, fcntl(mon->pipefd[0], F_GETFL, 0) | O_NONBLOCK);
    return mon;
}

struct udev_monitor *
udev_monitor_ref(struct udev_monitor *mon)
{
    if (mon)
        mon->refcount++;
    return mon;
}

struct udev_monitor *
udev_monitor_unref(struct udev_monitor *mon)
{
    if (mon && --mon->refcount == 0) {
        udev_unref(mon->udev);
        free(mon->subsystem);
        free(mon->devtype);
        if (mon->pipefd[0] >= 0)
            close(mon->pipefd[0]);
        if (mon->pipefd[1] >= 0)
            close(mon->pipefd[1]);
        free(mon);
    }
    return NULL;
}

int
udev_monitor_filter_add_match_subsystem_devtype(struct udev_monitor *mon,
                                                const char *subsystem,
                                                const char *devtype)
{
    if (!mon)
        return -EINVAL;
    free(mon->subsystem);
    free(mon->devtype);
    mon->subsystem = xstrdup(subsystem);
    mon->devtype = xstrdup(devtype);
    if ((subsystem && !mon->subsystem) || (devtype && !mon->devtype))
        return -ENOMEM;
    return 0;
}

int
udev_monitor_enable_receiving(struct udev_monitor *mon)
{
    return mon ? 0 : -EINVAL;
}

int
udev_monitor_get_fd(struct udev_monitor *mon)
{
    return mon ? mon->pipefd[0] : -1;
}

struct udev_device *
udev_monitor_receive_device(struct udev_monitor *mon)
{
    (void)mon;
    errno = EAGAIN;
    return NULL;
}
