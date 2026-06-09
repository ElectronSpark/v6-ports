#include "libinput.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <libudev.h>

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
};

struct libinput {
    const struct libinput_interface *interface;
    void *user_data;
    struct udev *udev;
    struct libinput_device *device;
    struct libinput_event *head;
    struct libinput_event *tail;
    int pipefd[2];
    int assigned;
    int resumed;
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
    if (li && li->pipefd[1] >= 0)
        (void)write(li->pipefd[1], &ch, 1);
}

static int
queue_event(struct libinput *li, enum libinput_event_type type)
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
    if (type == LIBINPUT_EVENT_POINTER_MOTION) {
        ev->dx = 0.0;
        ev->dy = 0.0;
    }
    if (li->tail)
        li->tail->next = ev;
    else
        li->head = ev;
    li->tail = ev;
    signal_fd(li);
    return 0;
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
    return li;
}

struct libinput *
libinput_unref(struct libinput *li)
{
    if (!li)
        return NULL;
    while (li->head) {
        struct libinput_event *next = li->head->next;
        libinput_device_unref(li->head->device);
        free(li->head);
        li->head = next;
    }
    if (li->device)
        libinput_device_unref(li->device);
    if (li->udev)
        udev_unref(li->udev);
    if (li->pipefd[0] >= 0)
        close(li->pipefd[0]);
    if (li->pipefd[1] >= 0)
        close(li->pipefd[1]);
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
    queue_event(li, LIBINPUT_EVENT_DEVICE_ADDED);
    queue_event(li, LIBINPUT_EVENT_POINTER_MOTION);
    return 0;
}

int
libinput_get_fd(struct libinput *li)
{
    return li ? li->pipefd[0] : -1;
}

int
libinput_dispatch(struct libinput *li)
{
    char buf[32];
    if (!li)
        return -1;
    while (read(li->pipefd[0], buf, sizeof(buf)) > 0)
        ;
    return 0;
}

struct libinput_event *
libinput_get_event(struct libinput *li)
{
    struct libinput_event *ev;
    if (!li || !li->head)
        return NULL;
    ev = li->head;
    li->head = ev->next;
    if (!li->head)
        li->tail = NULL;
    ev->next = NULL;
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
        queue_event(li, LIBINPUT_EVENT_DEVICE_ADDED);
        queue_event(li, LIBINPUT_EVENT_POINTER_MOTION);
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

uint32_t libinput_event_keyboard_get_key(struct libinput_event_keyboard *event) { (void)event; return 0; }
enum libinput_key_state libinput_event_keyboard_get_key_state(struct libinput_event_keyboard *event) { (void)event; return LIBINPUT_KEY_STATE_RELEASED; }
uint32_t libinput_event_keyboard_get_seat_key_count(struct libinput_event_keyboard *event) { (void)event; return 0; }
uint64_t libinput_event_keyboard_get_time_usec(struct libinput_event_keyboard *event) { return ((struct libinput_event *)event)->time_usec; }

double libinput_event_pointer_get_dx(struct libinput_event_pointer *event) { return ((struct libinput_event *)event)->dx; }
double libinput_event_pointer_get_dy(struct libinput_event_pointer *event) { return ((struct libinput_event *)event)->dy; }
double libinput_event_pointer_get_dx_unaccelerated(struct libinput_event_pointer *event) { return ((struct libinput_event *)event)->dx; }
double libinput_event_pointer_get_dy_unaccelerated(struct libinput_event_pointer *event) { return ((struct libinput_event *)event)->dy; }
double libinput_event_pointer_get_absolute_x_transformed(struct libinput_event_pointer *event, uint32_t width) { (void)event; return width / 2.0; }
double libinput_event_pointer_get_absolute_y_transformed(struct libinput_event_pointer *event, uint32_t height) { (void)event; return height / 2.0; }
uint32_t libinput_event_pointer_get_button(struct libinput_event_pointer *event) { (void)event; return 0; }
enum libinput_button_state libinput_event_pointer_get_button_state(struct libinput_event_pointer *event) { (void)event; return LIBINPUT_BUTTON_STATE_RELEASED; }
uint32_t libinput_event_pointer_get_seat_button_count(struct libinput_event_pointer *event) { (void)event; return 0; }
uint64_t libinput_event_pointer_get_time_usec(struct libinput_event_pointer *event) { return ((struct libinput_event *)event)->time_usec; }
int libinput_event_pointer_has_axis(struct libinput_event_pointer *event, enum libinput_pointer_axis axis) { (void)event; (void)axis; return 0; }
enum libinput_pointer_axis_source libinput_event_pointer_get_axis_source(struct libinput_event_pointer *event) { (void)event; return LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS; }
double libinput_event_pointer_get_axis_value(struct libinput_event_pointer *event, enum libinput_pointer_axis axis) { (void)event; (void)axis; return 0.0; }
int32_t libinput_event_pointer_get_axis_value_discrete(struct libinput_event_pointer *event, enum libinput_pointer_axis axis) { (void)event; (void)axis; return 0; }

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
