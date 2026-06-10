#include "libinput.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <libudev.h>

#define XV6_MOUSE_EVENT_F_ABSOLUTE 0x01
#define XV6_BTN_LEFT   0x110
#define XV6_BTN_RIGHT  0x111
#define XV6_BTN_MIDDLE 0x112

#define XV6_KBD_KEY_UP      0x80
#define XV6_KBD_KEY_DOWN    0x81
#define XV6_KBD_KEY_LEFT    0x82
#define XV6_KBD_KEY_RIGHT   0x83
#define XV6_KBD_KEY_HOME    0x84
#define XV6_KBD_KEY_END     0x85
#define XV6_KBD_KEY_PGUP    0x86
#define XV6_KBD_KEY_PGDN    0x87
#define XV6_KBD_KEY_INSERT  0x88
#define XV6_KBD_KEY_DELETE  0x89

#define KEY_RIGHTCTRL 97
#define KEY_HOME      102
#define KEY_UP        103
#define KEY_PAGEUP    104
#define KEY_LEFT      105
#define KEY_RIGHT     106
#define KEY_END       107
#define KEY_DOWN      108
#define KEY_PAGEDOWN  109
#define KEY_INSERT    110
#define KEY_DELETE    111

struct xv6_mouse_event {
    int16_t dx;
    int16_t dy;
    uint8_t buttons;
    uint8_t flags;
    int8_t dz;
    uint8_t pad[1];
};

struct xv6_kbd_event {
    uint8_t keycode;
    uint8_t scancode;
    uint8_t pressed;
    uint8_t modifiers;
};

struct libinput_seat {
    const char *name;
};

struct libinput_device {
    int refcount;
    void *user_data;
    struct libinput_seat seat;
    struct udev *udev;
};

struct libinput_event {
    enum libinput_event_type type;
    struct libinput *ctx;
    struct libinput_device *device;
    struct libinput_event *next;
    uint64_t time_usec;
    double dx;
    double dy;
    double abs_x;
    double abs_y;
    double axis_v;
    uint32_t button;
    uint32_t key;
    enum libinput_button_state button_state;
    enum libinput_key_state key_state;
    uint32_t seat_button_count;
    int has_axis_v;
    int32_t axis_discrete_v;
};

struct libinput {
    const struct libinput_interface *interface;
    void *user_data;
    struct udev *udev;
    struct libinput_device *device;
    struct libinput_event *head;
    struct libinput_event *tail;
    int pipefd[2];
    int mousefd;
    int kbdfd;
    pthread_t mouse_thread;
    pthread_mutex_t lock;
    int assigned;
    int resumed;
    volatile int mouse_thread_running;
    uint8_t buttons;
    libinput_log_handler log_handler;
    enum libinput_log_priority log_priority;
};

static uint64_t
now_usec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ull + (uint64_t)tv.tv_usec;
}

static void
signal_fd(struct libinput *li)
{
    char ch = 'i';
    if (li && li->pipefd[1] >= 0) {
        ssize_t n = write(li->pipefd[1], &ch, 1);
        (void)n;
    }
}

static int
queue_event_locked(struct libinput *li, enum libinput_event_type type,
                   struct libinput_event **out)
{
    struct libinput_event *ev;
    if (!li || !li->device)
        return -EINVAL;
    ev = calloc(1, sizeof(*ev));
    if (!ev)
        return -ENOMEM;
    ev->type = type;
    ev->ctx = li;
    ev->device = libinput_device_ref(li->device);
    ev->time_usec = now_usec();
    if (li->tail)
        li->tail->next = ev;
    else
        li->head = ev;
    li->tail = ev;
    if (out)
        *out = ev;
    return 0;
}

static int
queue_event(struct libinput *li, enum libinput_event_type type,
            struct libinput_event **out)
{
    int ret;

    pthread_mutex_lock(&li->lock);
    ret = queue_event_locked(li, type, out);
    pthread_mutex_unlock(&li->lock);
    if (ret == 0)
        signal_fd(li);
    return ret;
}

static void
queue_pointer_motion_locked(struct libinput *li,
                            const struct xv6_mouse_event *mev)
{
    struct libinput_event *ev = NULL;
    int absolute = (mev->flags & XV6_MOUSE_EVENT_F_ABSOLUTE) != 0;

    if (queue_event_locked(li,
                           absolute ? LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE :
                                      LIBINPUT_EVENT_POINTER_MOTION, &ev) != 0 ||
        !ev)
        return;
    if (absolute) {
        ev->abs_x = (double)(uint16_t)mev->dx;
        ev->abs_y = (double)(uint16_t)mev->dy;
    } else {
        ev->dx = (double)mev->dx;
        ev->dy = (double)mev->dy;
    }
}

static void
queue_pointer_button_locked(struct libinput *li, uint32_t button,
                            enum libinput_button_state state,
                            uint32_t seat_button_count)
{
    struct libinput_event *ev = NULL;

    if (queue_event_locked(li, LIBINPUT_EVENT_POINTER_BUTTON, &ev) != 0 || !ev)
        return;
    ev->button = button;
    ev->button_state = state;
    ev->seat_button_count = seat_button_count;
}

static uint32_t
button_count(uint8_t buttons)
{
    uint32_t count = 0;

    for (int i = 0; i < 3; i++)
        if (buttons & (1u << i))
            count++;
    return count;
}

static void
queue_pointer_axis_locked(struct libinput *li, int dz)
{
    struct libinput_event *ev = NULL;

    if (dz == 0)
        return;
    if (queue_event_locked(li, LIBINPUT_EVENT_POINTER_AXIS, &ev) != 0 || !ev)
        return;
    ev->has_axis_v = 1;
    ev->axis_v = (double)dz * 10.0;
    ev->axis_discrete_v = dz;
}

static uint32_t
kbd_event_key(const struct xv6_kbd_event *kev)
{
    switch (kev->keycode) {
    case XV6_KBD_KEY_UP:
        return KEY_UP;
    case XV6_KBD_KEY_DOWN:
        return KEY_DOWN;
    case XV6_KBD_KEY_LEFT:
        return KEY_LEFT;
    case XV6_KBD_KEY_RIGHT:
        return KEY_RIGHT;
    case XV6_KBD_KEY_HOME:
        return KEY_HOME;
    case XV6_KBD_KEY_END:
        return KEY_END;
    case XV6_KBD_KEY_PGUP:
        return KEY_PAGEUP;
    case XV6_KBD_KEY_PGDN:
        return KEY_PAGEDOWN;
    case XV6_KBD_KEY_INSERT:
        return KEY_INSERT;
    case XV6_KBD_KEY_DELETE:
        return KEY_DELETE;
    default:
        break;
    }

    if (kev->scancode == 0)
        return 0;
    return kev->scancode;
}

static void
queue_keyboard_key_locked(struct libinput *li, const struct xv6_kbd_event *kev)
{
    struct libinput_event *ev = NULL;
    uint32_t key = kbd_event_key(kev);

    if (key == 0)
        return;
    if (queue_event_locked(li, LIBINPUT_EVENT_KEYBOARD_KEY, &ev) != 0 || !ev)
        return;
    ev->key = key;
    ev->key_state = kev->pressed ? LIBINPUT_KEY_STATE_PRESSED :
                                   LIBINPUT_KEY_STATE_RELEASED;
    ev->seat_button_count = kev->pressed ? 1 : 0;
}

static void
queue_mouse_buttons_locked(struct libinput *li, uint8_t old_buttons,
                           uint8_t new_buttons)
{
    static const uint32_t map[3] = {
        XV6_BTN_LEFT,
        XV6_BTN_RIGHT,
        XV6_BTN_MIDDLE,
    };

    for (int i = 0; i < 3; i++) {
        uint8_t mask = (uint8_t)(1u << i);

        if ((old_buttons & mask) == (new_buttons & mask))
            continue;
        queue_pointer_button_locked(li, map[i],
            (new_buttons & mask) ? LIBINPUT_BUTTON_STATE_PRESSED :
                                   LIBINPUT_BUTTON_STATE_RELEASED,
            button_count(new_buttons));
    }
}

static void
dispatch_mouse(struct libinput *li)
{
    struct xv6_mouse_event mev;

    if (!li || li->mousefd < 0)
        return;
    while (read(li->mousefd, &mev, sizeof(mev)) == (ssize_t)sizeof(mev)) {
        uint8_t old_buttons = li->buttons;

        pthread_mutex_lock(&li->lock);
        old_buttons = li->buttons;
        queue_pointer_motion_locked(li, &mev);
        li->buttons = mev.buttons;
        queue_mouse_buttons_locked(li, old_buttons, li->buttons);
        queue_pointer_axis_locked(li, mev.dz);
        pthread_mutex_unlock(&li->lock);
        signal_fd(li);
    }
}

static void
dispatch_keyboard(struct libinput *li)
{
    struct xv6_kbd_event kev;

    if (!li || li->kbdfd < 0)
        return;
    while (read(li->kbdfd, &kev, sizeof(kev)) == (ssize_t)sizeof(kev)) {
        pthread_mutex_lock(&li->lock);
        queue_keyboard_key_locked(li, &kev);
        pthread_mutex_unlock(&li->lock);
        signal_fd(li);
    }
}

static void *
mouse_thread_main(void *arg)
{
    struct libinput *li = arg;

    while (li->mouse_thread_running) {
        dispatch_mouse(li);
        dispatch_keyboard(li);
        usleep(5000);
    }
    return NULL;
}

struct libinput *
libinput_udev_create_context(const struct libinput_interface *interface,
                             void *user_data,
                             struct udev *udev)
{
    struct libinput *li = calloc(1, sizeof(*li));
    if (!li)
        return NULL;
    li->interface = interface;
    li->user_data = user_data;
    li->udev = udev_ref(udev);
    li->pipefd[0] = -1;
    li->pipefd[1] = -1;
    li->mousefd = -1;
    li->kbdfd = -1;
    pthread_mutex_init(&li->lock, NULL);
    if (pipe(li->pipefd) != 0) {
        libinput_unref(li);
        return NULL;
    }
    fcntl(li->pipefd[0], F_SETFL, fcntl(li->pipefd[0], F_GETFL, 0) | O_NONBLOCK);
    fcntl(li->pipefd[1], F_SETFL, fcntl(li->pipefd[1], F_GETFL, 0) | O_NONBLOCK);
    li->device = calloc(1, sizeof(*li->device));
    if (!li->device) {
        libinput_unref(li);
        return NULL;
    }
    li->device->refcount = 1;
    li->device->seat.name = "seat0";
    li->device->udev = udev_ref(udev);
    li->mousefd = open("/dev/mouse", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    li->kbdfd = open("/dev/kbd", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (li->mousefd >= 0 || li->kbdfd >= 0) {
        li->mouse_thread_running = 1;
        if (pthread_create(&li->mouse_thread, NULL, mouse_thread_main, li) != 0)
            li->mouse_thread_running = 0;
    }
    return li;
}

struct libinput *
libinput_unref(struct libinput *li)
{
    if (!li)
        return NULL;
    if (li->mouse_thread_running) {
        li->mouse_thread_running = 0;
        pthread_join(li->mouse_thread, NULL);
    }
    pthread_mutex_lock(&li->lock);
    while (li->head) {
        struct libinput_event *next = li->head->next;
        libinput_device_unref(li->head->device);
        free(li->head);
        li->head = next;
    }
    pthread_mutex_unlock(&li->lock);
    if (li->device)
        libinput_device_unref(li->device);
    if (li->udev)
        udev_unref(li->udev);
    if (li->mousefd >= 0)
        close(li->mousefd);
    if (li->kbdfd >= 0)
        close(li->kbdfd);
    if (li->pipefd[0] >= 0)
        close(li->pipefd[0]);
    if (li->pipefd[1] >= 0)
        close(li->pipefd[1]);
    pthread_mutex_destroy(&li->lock);
    free(li);
    return NULL;
}

int
libinput_udev_assign_seat(struct libinput *li, const char *seat_id)
{
    if (!li)
        return -1;
    li->assigned = 1;
    if (seat_id && li->device)
        li->device->seat.name = "seat0";
    queue_event(li, LIBINPUT_EVENT_DEVICE_ADDED, NULL);
    return 0;
}

int
libinput_get_fd(struct libinput *li)
{
    if (!li)
        return -1;
    return li->pipefd[0];
}

int
libinput_dispatch(struct libinput *li)
{
    char buf[32];
    if (!li)
        return -1;
    while (read(li->pipefd[0], buf, sizeof(buf)) > 0)
        ;
    if (!li->mouse_thread_running) {
        dispatch_mouse(li);
        dispatch_keyboard(li);
    }
    return 0;
}

struct libinput_event *
libinput_get_event(struct libinput *li)
{
    struct libinput_event *ev;
    if (!li || !li->head)
        return NULL;
    pthread_mutex_lock(&li->lock);
    if (!li->head) {
        pthread_mutex_unlock(&li->lock);
        return NULL;
    }
    ev = li->head;
    li->head = ev->next;
    if (!li->head)
        li->tail = NULL;
    ev->next = NULL;
    pthread_mutex_unlock(&li->lock);
    return ev;
}

void *
libinput_get_user_data(struct libinput *li)
{
    return li ? li->user_data : NULL;
}

void
libinput_log_set_handler(struct libinput *li, libinput_log_handler handler)
{
    if (li)
        li->log_handler = handler;
}

void
libinput_log_set_priority(struct libinput *li, enum libinput_log_priority priority)
{
    if (li)
        li->log_priority = priority;
}

int
libinput_resume(struct libinput *li)
{
    if (!li)
        return -1;
    li->resumed = 1;
    if (!li->head) {
        queue_event(li, LIBINPUT_EVENT_DEVICE_ADDED, NULL);
    }
    return 0;
}

void
libinput_suspend(struct libinput *li)
{
    if (li)
        li->resumed = 0;
}

enum libinput_event_type
libinput_event_get_type(struct libinput_event *event)
{
    return event ? event->type : LIBINPUT_EVENT_NONE;
}

struct libinput *
libinput_event_get_context(struct libinput_event *event)
{
    return event ? event->ctx : NULL;
}

struct libinput_device *
libinput_event_get_device(struct libinput_event *event)
{
    return event ? event->device : NULL;
}

void
libinput_event_destroy(struct libinput_event *event)
{
    if (!event)
        return;
    libinput_device_unref(event->device);
    free(event);
}

struct libinput_event_keyboard *libinput_event_get_keyboard_event(struct libinput_event *event) { return (void *)event; }
struct libinput_event_pointer *libinput_event_get_pointer_event(struct libinput_event *event) { return (void *)event; }
struct libinput_event_touch *libinput_event_get_touch_event(struct libinput_event *event) { return (void *)event; }
struct libinput_event_tablet_tool *libinput_event_get_tablet_tool_event(struct libinput_event *event) { return (void *)event; }

int
libinput_device_has_capability(struct libinput_device *device,
                               enum libinput_device_capability capability)
{
    (void)device;
    return capability == LIBINPUT_DEVICE_CAP_KEYBOARD ||
           capability == LIBINPUT_DEVICE_CAP_POINTER;
}

struct libinput_device *
libinput_device_ref(struct libinput_device *device)
{
    if (device)
        device->refcount++;
    return device;
}

struct libinput_device *
libinput_device_unref(struct libinput_device *device)
{
    if (device && --device->refcount == 0) {
        if (device->udev)
            udev_unref(device->udev);
        free(device);
    }
    return NULL;
}

void libinput_device_set_user_data(struct libinput_device *device, void *user_data) { if (device) device->user_data = user_data; }
void *libinput_device_get_user_data(struct libinput_device *device) { return device ? device->user_data : NULL; }
const char *libinput_device_get_name(struct libinput_device *device) { (void)device; return "xv6 keyboard pointer"; }
const char *libinput_device_get_sysname(struct libinput_device *device) { (void)device; return "xv6-input0"; }
const char *libinput_device_get_output_name(struct libinput_device *device) { (void)device; return NULL; }
struct libinput_seat *libinput_device_get_seat(struct libinput_device *device) { return device ? &device->seat : NULL; }
struct udev_device *libinput_device_get_udev_device(struct libinput_device *device) { (void)device; return NULL; }
unsigned int libinput_device_get_id_vendor(struct libinput_device *device) { (void)device; return 0; }
unsigned int libinput_device_get_id_product(struct libinput_device *device) { (void)device; return 0; }
void libinput_device_led_update(struct libinput_device *device, enum libinput_led leds) { (void)device; (void)leds; }

int libinput_device_config_calibration_has_matrix(struct libinput_device *device) { (void)device; return 0; }
int libinput_device_config_calibration_get_default_matrix(struct libinput_device *device, float matrix[6]) { (void)device; if (matrix) memset(matrix, 0, sizeof(float) * 6); return -1; }
int libinput_device_config_calibration_get_matrix(struct libinput_device *device, float matrix[6]) { return libinput_device_config_calibration_get_default_matrix(device, matrix); }
enum libinput_config_status libinput_device_config_calibration_set_matrix(struct libinput_device *device, const float matrix[6]) { (void)device; (void)matrix; return LIBINPUT_CONFIG_STATUS_UNSUPPORTED; }
const char *libinput_seat_get_logical_name(struct libinput_seat *seat) { return seat ? seat->name : "seat0"; }

int libinput_device_config_accel_is_available(struct libinput_device *device) { (void)device; return 0; }
uint32_t libinput_device_config_accel_get_profiles(struct libinput_device *device) { (void)device; return 0; }
enum libinput_config_status libinput_device_config_accel_set_profile(struct libinput_device *device, enum libinput_config_accel_profile profile) { (void)device; (void)profile; return LIBINPUT_CONFIG_STATUS_UNSUPPORTED; }
enum libinput_config_status libinput_device_config_accel_set_speed(struct libinput_device *device, double speed) { (void)device; (void)speed; return LIBINPUT_CONFIG_STATUS_UNSUPPORTED; }
int libinput_device_config_scroll_has_natural_scroll(struct libinput_device *device) { (void)device; return 0; }
enum libinput_config_status libinput_device_config_scroll_set_natural_scroll_enabled(struct libinput_device *device, int enabled) { (void)device; (void)enabled; return LIBINPUT_CONFIG_STATUS_UNSUPPORTED; }
uint32_t libinput_device_config_scroll_get_methods(struct libinput_device *device) { (void)device; return 0; }
enum libinput_config_status libinput_device_config_scroll_set_method(struct libinput_device *device, enum libinput_config_scroll_method method) { (void)device; (void)method; return LIBINPUT_CONFIG_STATUS_UNSUPPORTED; }
enum libinput_config_status libinput_device_config_scroll_set_button(struct libinput_device *device, uint32_t button) { (void)device; (void)button; return LIBINPUT_CONFIG_STATUS_UNSUPPORTED; }
int libinput_device_config_tap_get_finger_count(struct libinput_device *device) { (void)device; return 0; }
enum libinput_config_status libinput_device_config_tap_set_enabled(struct libinput_device *device, int enabled) { (void)device; (void)enabled; return LIBINPUT_CONFIG_STATUS_UNSUPPORTED; }
enum libinput_config_status libinput_device_config_tap_set_drag_enabled(struct libinput_device *device, int enabled) { (void)device; (void)enabled; return LIBINPUT_CONFIG_STATUS_UNSUPPORTED; }
enum libinput_config_status libinput_device_config_tap_set_drag_lock_enabled(struct libinput_device *device, int enabled) { (void)device; (void)enabled; return LIBINPUT_CONFIG_STATUS_UNSUPPORTED; }
int libinput_device_config_dwt_is_available(struct libinput_device *device) { (void)device; return 0; }
enum libinput_config_status libinput_device_config_dwt_set_enabled(struct libinput_device *device, int enabled) { (void)device; (void)enabled; return LIBINPUT_CONFIG_STATUS_UNSUPPORTED; }
int libinput_device_config_middle_emulation_is_available(struct libinput_device *device) { (void)device; return 0; }
enum libinput_config_status libinput_device_config_middle_emulation_set_enabled(struct libinput_device *device, int enabled) { (void)device; (void)enabled; return LIBINPUT_CONFIG_STATUS_UNSUPPORTED; }
int libinput_device_config_left_handed_is_available(struct libinput_device *device) { (void)device; return 0; }
enum libinput_config_status libinput_device_config_left_handed_set(struct libinput_device *device, int enabled) { (void)device; (void)enabled; return LIBINPUT_CONFIG_STATUS_UNSUPPORTED; }
int libinput_device_config_rotation_is_available(struct libinput_device *device) { (void)device; return 0; }
enum libinput_config_status libinput_device_config_rotation_set_angle(struct libinput_device *device, unsigned int degrees) { (void)device; (void)degrees; return LIBINPUT_CONFIG_STATUS_UNSUPPORTED; }

uint32_t libinput_event_keyboard_get_key(struct libinput_event_keyboard *event) { return ((struct libinput_event *)event)->key; }
enum libinput_key_state libinput_event_keyboard_get_key_state(struct libinput_event_keyboard *event) { return ((struct libinput_event *)event)->key_state; }
uint32_t libinput_event_keyboard_get_seat_key_count(struct libinput_event_keyboard *event) { return ((struct libinput_event *)event)->seat_button_count; }
uint64_t libinput_event_keyboard_get_time_usec(struct libinput_event_keyboard *event) { return ((struct libinput_event *)event)->time_usec; }

double libinput_event_pointer_get_dx(struct libinput_event_pointer *event) { return ((struct libinput_event *)event)->dx; }
double libinput_event_pointer_get_dy(struct libinput_event_pointer *event) { return ((struct libinput_event *)event)->dy; }
double libinput_event_pointer_get_dx_unaccelerated(struct libinput_event_pointer *event) { return ((struct libinput_event *)event)->dx; }
double libinput_event_pointer_get_dy_unaccelerated(struct libinput_event_pointer *event) { return ((struct libinput_event *)event)->dy; }
double libinput_event_pointer_get_absolute_x_transformed(struct libinput_event_pointer *event, uint32_t width) { return ((struct libinput_event *)event)->abs_x * (double)width / 65535.0; }
double libinput_event_pointer_get_absolute_y_transformed(struct libinput_event_pointer *event, uint32_t height) { return ((struct libinput_event *)event)->abs_y * (double)height / 65535.0; }
uint32_t libinput_event_pointer_get_button(struct libinput_event_pointer *event) { return ((struct libinput_event *)event)->button; }
enum libinput_button_state libinput_event_pointer_get_button_state(struct libinput_event_pointer *event) { return ((struct libinput_event *)event)->button_state; }
uint32_t libinput_event_pointer_get_seat_button_count(struct libinput_event_pointer *event) { return ((struct libinput_event *)event)->seat_button_count; }
uint64_t libinput_event_pointer_get_time_usec(struct libinput_event_pointer *event) { return ((struct libinput_event *)event)->time_usec; }
int libinput_event_pointer_has_axis(struct libinput_event_pointer *event, enum libinput_pointer_axis axis) { return axis == LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL && ((struct libinput_event *)event)->has_axis_v; }
enum libinput_pointer_axis_source libinput_event_pointer_get_axis_source(struct libinput_event_pointer *event) { (void)event; return LIBINPUT_POINTER_AXIS_SOURCE_WHEEL; }
double libinput_event_pointer_get_axis_value(struct libinput_event_pointer *event, enum libinput_pointer_axis axis) { return axis == LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL ? ((struct libinput_event *)event)->axis_v : 0.0; }
int32_t libinput_event_pointer_get_axis_value_discrete(struct libinput_event_pointer *event, enum libinput_pointer_axis axis) { return axis == LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL ? ((struct libinput_event *)event)->axis_discrete_v : 0; }

int32_t libinput_event_touch_get_seat_slot(struct libinput_event_touch *event) { (void)event; return 0; }
uint64_t libinput_event_touch_get_time_usec(struct libinput_event_touch *event) { return ((struct libinput_event *)event)->time_usec; }
double libinput_event_touch_get_x_transformed(struct libinput_event_touch *event, uint32_t width) { (void)event; return width / 2.0; }
double libinput_event_touch_get_y_transformed(struct libinput_event_touch *event, uint32_t height) { (void)event; return height / 2.0; }

struct libinput_tablet_tool *libinput_event_tablet_tool_get_tool(struct libinput_event_tablet_tool *event) { (void)event; return NULL; }
uint32_t libinput_event_tablet_tool_get_time(struct libinput_event_tablet_tool *event) { return (uint32_t)(((struct libinput_event *)event)->time_usec / 1000); }
double libinput_event_tablet_tool_get_x_transformed(struct libinput_event_tablet_tool *event, uint32_t width) { (void)event; return width / 2.0; }
double libinput_event_tablet_tool_get_y_transformed(struct libinput_event_tablet_tool *event, uint32_t height) { (void)event; return height / 2.0; }
double libinput_event_tablet_tool_get_pressure(struct libinput_event_tablet_tool *event) { (void)event; return 0.0; }
double libinput_event_tablet_tool_get_distance(struct libinput_event_tablet_tool *event) { (void)event; return 0.0; }
double libinput_event_tablet_tool_get_tilt_x(struct libinput_event_tablet_tool *event) { (void)event; return 0.0; }
double libinput_event_tablet_tool_get_tilt_y(struct libinput_event_tablet_tool *event) { (void)event; return 0.0; }
int libinput_event_tablet_tool_x_has_changed(struct libinput_event_tablet_tool *event) { (void)event; return 0; }
int libinput_event_tablet_tool_y_has_changed(struct libinput_event_tablet_tool *event) { (void)event; return 0; }
int libinput_event_tablet_tool_pressure_has_changed(struct libinput_event_tablet_tool *event) { (void)event; return 0; }
int libinput_event_tablet_tool_distance_has_changed(struct libinput_event_tablet_tool *event) { (void)event; return 0; }
int libinput_event_tablet_tool_tilt_x_has_changed(struct libinput_event_tablet_tool *event) { (void)event; return 0; }
int libinput_event_tablet_tool_tilt_y_has_changed(struct libinput_event_tablet_tool *event) { (void)event; return 0; }
enum libinput_tablet_tool_proximity_state libinput_event_tablet_tool_get_proximity_state(struct libinput_event_tablet_tool *event) { (void)event; return LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT; }
enum libinput_tablet_tool_tip_state libinput_event_tablet_tool_get_tip_state(struct libinput_event_tablet_tool *event) { (void)event; return LIBINPUT_TABLET_TOOL_TIP_UP; }
uint32_t libinput_event_tablet_tool_get_button(struct libinput_event_tablet_tool *event) { (void)event; return 0; }
enum libinput_button_state libinput_event_tablet_tool_get_button_state(struct libinput_event_tablet_tool *event) { (void)event; return LIBINPUT_BUTTON_STATE_RELEASED; }

enum libinput_tablet_tool_type libinput_tablet_tool_get_type(struct libinput_tablet_tool *tool) { (void)tool; return LIBINPUT_TABLET_TOOL_TYPE_PEN; }
uint64_t libinput_tablet_tool_get_serial(struct libinput_tablet_tool *tool) { (void)tool; return 0; }
uint64_t libinput_tablet_tool_get_tool_id(struct libinput_tablet_tool *tool) { (void)tool; return 0; }
int libinput_tablet_tool_is_unique(struct libinput_tablet_tool *tool) { (void)tool; return 0; }
int libinput_tablet_tool_has_pressure(struct libinput_tablet_tool *tool) { (void)tool; return 0; }
int libinput_tablet_tool_has_distance(struct libinput_tablet_tool *tool) { (void)tool; return 0; }
int libinput_tablet_tool_has_tilt(struct libinput_tablet_tool *tool) { (void)tool; return 0; }
void libinput_tablet_tool_set_user_data(struct libinput_tablet_tool *tool, void *user_data) { (void)tool; (void)user_data; }
void *libinput_tablet_tool_get_user_data(struct libinput_tablet_tool *tool) { (void)tool; return NULL; }
