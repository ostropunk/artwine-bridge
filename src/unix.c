/* Artwine Bridge, Unix half: runs inside the Wine process and calls the system libusb-1.0. */
#include <libusb.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "common.h"

typedef int32_t NTSTATUS;
typedef NTSTATUS (*unixlib_entry_t)(void *args);

#define P(i) ((void *)(uintptr_t)args->a[i])
#define I(i) ((int)(int64_t)args->a[i])
#define U(i) ((unsigned)args->a[i])

/* ---- optional call log: set ARTWINE_LOG=/path/to/file ---- */
static FILE *logf;
static pthread_once_t log_once = PTHREAD_ONCE_INIT;
static void log_open(void)
{
    const char *p = getenv("ARTWINE_LOG");
    if (p && *p) logf = fopen(p, "a");
    if (logf) setvbuf(logf, NULL, _IOLBF, 0);
}
static void blog(const char *fmt, ...)
{
    pthread_once(&log_once, log_open);
    if (!logf) return;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    fprintf(logf, "%ld.%03ld ", (long)ts.tv_sec, ts.tv_nsec / 1000000);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fputc('\n', logf);
}

/* ---- hotplug: native callbacks queue events; the PE side drains them ---- */
struct hp_event {
    struct hp_event *next;
    int slot;
    int event;
    libusb_device *dev;
};
static pthread_mutex_t hp_lock = PTHREAD_MUTEX_INITIALIZER;
static struct hp_event *hp_head, *hp_tail;

static int LIBUSB_CALL hp_native_cb(libusb_context *ctx, libusb_device *dev,
                                    libusb_hotplug_event event, void *user)
{
    (void)ctx;
    struct hp_event *e = calloc(1, sizeof(*e));
    if (!e) return 0;
    e->slot = (int)(intptr_t)user;
    e->event = event;
    e->dev = libusb_ref_device(dev);
    pthread_mutex_lock(&hp_lock);
    if (hp_tail) hp_tail->next = e; else hp_head = e;
    hp_tail = e;
    pthread_mutex_unlock(&hp_lock);
    blog("hotplug queued slot=%d event=%d dev=%p", e->slot, event, (void *)dev);
    return 0;
}

/* ---- entry points ---- */
static NTSTATUS f_init_context(struct bridge_args *args)
{
    libusb_context **ctx = P(0);
    args->ret = libusb_init_context(ctx, NULL, 0);
    blog("init_context -> %lld ctx=%p", (long long)args->ret, ctx ? (void *)*ctx : NULL);
    return 0;
}
static NTSTATUS f_exit(struct bridge_args *args) { blog("exit"); libusb_exit(P(0)); return 0; }
static NTSTATUS f_set_debug(struct bridge_args *args)
{
    libusb_set_option(P(0), LIBUSB_OPTION_LOG_LEVEL, I(1));
    return 0;
}
static NTSTATUS f_get_device_list(struct bridge_args *args)
{
    args->ret = libusb_get_device_list(P(0), P(1));
    blog("get_device_list -> %lld", (long long)args->ret);
    return 0;
}
static NTSTATUS f_free_device_list(struct bridge_args *args) { libusb_free_device_list(P(0), I(1)); return 0; }
static NTSTATUS f_ref_device(struct bridge_args *args) { args->ret = (int64_t)(uintptr_t)libusb_ref_device(P(0)); return 0; }
static NTSTATUS f_unref_device(struct bridge_args *args) { libusb_unref_device(P(0)); return 0; }
static NTSTATUS f_get_device_descriptor(struct bridge_args *args)
{
    args->ret = libusb_get_device_descriptor(P(0), P(1));
    return 0;
}
static NTSTATUS f_get_active_config_descriptor(struct bridge_args *args)
{
    args->ret = libusb_get_active_config_descriptor(P(0), P(1));
    return 0;
}
static NTSTATUS f_get_config_descriptor(struct bridge_args *args)
{
    args->ret = libusb_get_config_descriptor(P(0), (uint8_t)args->a[1], P(2));
    return 0;
}
static NTSTATUS f_free_config_descriptor(struct bridge_args *args) { libusb_free_config_descriptor(P(0)); return 0; }
static NTSTATUS f_get_bus_number(struct bridge_args *args) { args->ret = libusb_get_bus_number(P(0)); return 0; }
static NTSTATUS f_get_device_address(struct bridge_args *args) { args->ret = libusb_get_device_address(P(0)); return 0; }
static NTSTATUS f_get_device_speed(struct bridge_args *args) { args->ret = libusb_get_device_speed(P(0)); return 0; }
static NTSTATUS f_get_port_number(struct bridge_args *args) { args->ret = libusb_get_port_number(P(0)); return 0; }
static NTSTATUS f_get_port_numbers(struct bridge_args *args)
{
    args->ret = libusb_get_port_numbers(P(0), P(1), I(2));
    return 0;
}
static NTSTATUS f_get_port_path(struct bridge_args *args)
{
    args->ret = libusb_get_port_numbers(P(1), P(2), (uint8_t)args->a[3]);
    return 0;
}
static NTSTATUS f_get_parent(struct bridge_args *args) { args->ret = (int64_t)(uintptr_t)libusb_get_parent(P(0)); return 0; }
static NTSTATUS f_open(struct bridge_args *args)
{
    libusb_device *dev = P(0);
    struct libusb_device_descriptor d;
    libusb_get_device_descriptor(dev, &d);
    args->ret = libusb_open(dev, P(1));
    blog("open %04x:%04x -> %lld (%s)", d.idVendor, d.idProduct, (long long)args->ret,
         args->ret ? libusb_error_name((int)args->ret) : "ok");
    return 0;
}
static NTSTATUS f_close(struct bridge_args *args) { blog("close %p", P(0)); libusb_close(P(0)); return 0; }
static NTSTATUS f_get_device(struct bridge_args *args) { args->ret = (int64_t)(uintptr_t)libusb_get_device(P(0)); return 0; }
static NTSTATUS f_get_configuration(struct bridge_args *args)
{
    args->ret = libusb_get_configuration(P(0), P(1));
    return 0;
}
static NTSTATUS f_set_configuration(struct bridge_args *args)
{
    /* Re-selecting the active configuration would fail (or reset the device)
     * while the kernel's audio/MIDI driver holds other interfaces. Treat it as
     * the no-op it is on Windows. */
    int cur = -1;
    if (libusb_get_configuration(P(0), &cur) == 0 && cur == I(1)) {
        blog("set_configuration %d (already active, skipped)", I(1));
        args->ret = 0;
        return 0;
    }
    args->ret = libusb_set_configuration(P(0), I(1));
    blog("set_configuration %d -> %lld", I(1), (long long)args->ret);
    return 0;
}
static NTSTATUS f_claim_interface(struct bridge_args *args)
{
    args->ret = libusb_claim_interface(P(0), I(1));
    blog("claim_interface %d -> %lld", I(1), (long long)args->ret);
    return 0;
}
static NTSTATUS f_release_interface(struct bridge_args *args)
{
    args->ret = libusb_release_interface(P(0), I(1));
    blog("release_interface %d -> %lld", I(1), (long long)args->ret);
    return 0;
}
static NTSTATUS f_set_interface_alt_setting(struct bridge_args *args)
{
    args->ret = libusb_set_interface_alt_setting(P(0), I(1), I(2));
    return 0;
}
static NTSTATUS f_clear_halt(struct bridge_args *args) { args->ret = libusb_clear_halt(P(0), (uint8_t)args->a[1]); return 0; }
static NTSTATUS f_reset_device(struct bridge_args *args)
{
    args->ret = libusb_reset_device(P(0));
    blog("reset_device -> %lld", (long long)args->ret);
    return 0;
}

/* Never take an interface away from the kernel's audio/MIDI driver:
 * that would cut the MiniFreak's MIDI port out from under Linux. */
static int is_audio_interface(libusb_device_handle *h, int iface)
{
    struct libusb_config_descriptor *cfg;
    int audio = 0;
    if (libusb_get_active_config_descriptor(libusb_get_device(h), &cfg)) return 0;
    for (int i = 0; i < cfg->bNumInterfaces; i++) {
        const struct libusb_interface *itf = &cfg->interface[i];
        if (itf->num_altsetting > 0 && itf->altsetting[0].bInterfaceNumber == iface)
            audio = itf->altsetting[0].bInterfaceClass == LIBUSB_CLASS_AUDIO;
    }
    libusb_free_config_descriptor(cfg);
    return audio;
}
static NTSTATUS f_kernel_driver_active(struct bridge_args *args)
{
    args->ret = is_audio_interface(P(0), I(1)) ? 0 : libusb_kernel_driver_active(P(0), I(1));
    return 0;
}
static NTSTATUS f_detach_kernel_driver(struct bridge_args *args)
{
    if (is_audio_interface(P(0), I(1))) {
        blog("detach_kernel_driver %d refused (audio interface)", I(1));
        args->ret = LIBUSB_ERROR_NOT_FOUND;
        return 0;
    }
    args->ret = libusb_detach_kernel_driver(P(0), I(1));
    blog("detach_kernel_driver %d -> %lld", I(1), (long long)args->ret);
    return 0;
}
static NTSTATUS f_attach_kernel_driver(struct bridge_args *args)
{
    args->ret = libusb_attach_kernel_driver(P(0), I(1));
    return 0;
}
static NTSTATUS f_set_auto_detach_kernel_driver(struct bridge_args *args)
{
    /* Windows has no kernel drivers to detach; keep Linux's in place. */
    (void)args;
    args->ret = 0;
    return 0;
}
static NTSTATUS f_control_transfer(struct bridge_args *args)
{
    args->ret = libusb_control_transfer(P(0), (uint8_t)args->a[1], (uint8_t)args->a[2],
                                        (uint16_t)args->a[3], (uint16_t)args->a[4], P(5),
                                        (uint16_t)args->a[6], U(7));
    blog("control_transfer type=%02x req=%02x val=%04x idx=%04x len=%u -> %lld",
         (unsigned)(uint8_t)args->a[1], (unsigned)(uint8_t)args->a[2], (unsigned)(uint16_t)args->a[3],
         (unsigned)(uint16_t)args->a[4], (unsigned)(uint16_t)args->a[6], (long long)args->ret);
    return 0;
}
static NTSTATUS f_bulk_transfer(struct bridge_args *args)
{
    int *xfer = P(4);
    args->ret = libusb_bulk_transfer(P(0), (uint8_t)args->a[1], P(2), I(3), xfer, U(5));
    blog("bulk_transfer ep=%02x len=%d -> %lld (%d bytes)", (unsigned)(uint8_t)args->a[1], I(3),
         (long long)args->ret, xfer ? *xfer : -1);
    return 0;
}
static NTSTATUS f_interrupt_transfer(struct bridge_args *args)
{
    args->ret = libusb_interrupt_transfer(P(0), (uint8_t)args->a[1], P(2), I(3), P(4), U(5));
    return 0;
}
static NTSTATUS f_get_string_descriptor_ascii(struct bridge_args *args)
{
    args->ret = libusb_get_string_descriptor_ascii(P(0), (uint8_t)args->a[1], P(2), I(3));
    return 0;
}
static NTSTATUS f_handle_events_timeout(struct bridge_args *args)
{
    struct timeval tv = { (time_t)(int64_t)args->a[1], (suseconds_t)(int64_t)args->a[2] };
    args->ret = libusb_handle_events_timeout(P(0), &tv);
    return 0;
}
static NTSTATUS f_handle_events_timeout_completed(struct bridge_args *args)
{
    struct timeval tv = { (time_t)(int64_t)args->a[1], (suseconds_t)(int64_t)args->a[2] };
    args->ret = libusb_handle_events_timeout_completed(P(0), &tv, P(3));
    return 0;
}
static NTSTATUS f_hotplug_register(struct bridge_args *args)
{
    args->ret = libusb_hotplug_register_callback(P(0), I(1), I(2), I(3), I(4), I(5), hp_native_cb,
                                                 (void *)(intptr_t)I(6), P(7));
    blog("hotplug_register slot=%d events=%d flags=%d vid=%04x pid=%04x -> %lld", I(6), I(1), I(2),
         I(3) & 0xffff, I(4) & 0xffff, (long long)args->ret);
    return 0;
}
static NTSTATUS f_hotplug_deregister(struct bridge_args *args)
{
    libusb_hotplug_deregister_callback(P(0), I(1));
    return 0;
}
/* a[0]=out slot*, a[1]=out event*, a[2]=out dev**; ret 1 if an event was popped */
static NTSTATUS f_hotplug_pop(struct bridge_args *args)
{
    pthread_mutex_lock(&hp_lock);
    struct hp_event *e = hp_head;
    if (e) {
        hp_head = e->next;
        if (!hp_head) hp_tail = NULL;
    }
    pthread_mutex_unlock(&hp_lock);
    args->ret = 0;
    if (!e) return 0;
    *(int *)P(0) = e->slot;
    *(int *)P(1) = e->event;
    *(libusb_device **)P(2) = e->dev; /* caller unrefs */
    free(e);
    args->ret = 1;
    return 0;
}
static NTSTATUS f_error_name(struct bridge_args *args) { args->ret = (int64_t)(uintptr_t)libusb_error_name(I(0)); return 0; }
static NTSTATUS f_strerror(struct bridge_args *args) { args->ret = (int64_t)(uintptr_t)libusb_strerror(I(0)); return 0; }
static NTSTATUS f_get_version(struct bridge_args *args) { args->ret = (int64_t)(uintptr_t)libusb_get_version(); return 0; }
static NTSTATUS f_has_capability(struct bridge_args *args) { args->ret = libusb_has_capability(U(0)); return 0; }
static NTSTATUS f_open_device_with_vid_pid(struct bridge_args *args)
{
    args->ret = (int64_t)(uintptr_t)libusb_open_device_with_vid_pid(P(0), (uint16_t)args->a[1], (uint16_t)args->a[2]);
    return 0;
}
static NTSTATUS f_get_max_packet_size(struct bridge_args *args)
{
    args->ret = libusb_get_max_packet_size(P(0), (unsigned char)args->a[1]);
    return 0;
}

static NTSTATUS f_get_device_string(struct bridge_args *args)
{
    args->ret = libusb_get_device_string(P(0), I(1), P(2), I(3));
    blog("get_device_string type=%d -> %lld", I(1), (long long)args->ret);
    return 0;
}
static NTSTATUS f_get_session_data(struct bridge_args *args)
{
    args->ret = (int64_t)libusb_get_session_data(P(0));
    return 0;
}
static NTSTATUS f_get_config_descriptor_by_value(struct bridge_args *args)
{
    args->ret = libusb_get_config_descriptor_by_value(P(0), (uint8_t)args->a[1], P(2));
    return 0;
}
static NTSTATUS f_get_max_iso_packet_size(struct bridge_args *args)
{
    args->ret = libusb_get_max_iso_packet_size(P(0), (unsigned char)args->a[1]);
    return 0;
}
static NTSTATUS f_get_max_alt_packet_size(struct bridge_args *args)
{
    args->ret = libusb_get_max_alt_packet_size(P(0), I(1), I(2), (unsigned char)args->a[3]);
    return 0;
}

__attribute__((visibility("default"))) const unixlib_entry_t __wine_unix_call_funcs[F_COUNT] = {
    [F_init_context] = (unixlib_entry_t)f_init_context,
    [F_exit] = (unixlib_entry_t)f_exit,
    [F_set_debug] = (unixlib_entry_t)f_set_debug,
    [F_get_device_list] = (unixlib_entry_t)f_get_device_list,
    [F_free_device_list] = (unixlib_entry_t)f_free_device_list,
    [F_ref_device] = (unixlib_entry_t)f_ref_device,
    [F_unref_device] = (unixlib_entry_t)f_unref_device,
    [F_get_device_descriptor] = (unixlib_entry_t)f_get_device_descriptor,
    [F_get_active_config_descriptor] = (unixlib_entry_t)f_get_active_config_descriptor,
    [F_get_config_descriptor] = (unixlib_entry_t)f_get_config_descriptor,
    [F_free_config_descriptor] = (unixlib_entry_t)f_free_config_descriptor,
    [F_get_bus_number] = (unixlib_entry_t)f_get_bus_number,
    [F_get_device_address] = (unixlib_entry_t)f_get_device_address,
    [F_get_device_speed] = (unixlib_entry_t)f_get_device_speed,
    [F_get_port_number] = (unixlib_entry_t)f_get_port_number,
    [F_get_port_numbers] = (unixlib_entry_t)f_get_port_numbers,
    [F_get_port_path] = (unixlib_entry_t)f_get_port_path,
    [F_get_parent] = (unixlib_entry_t)f_get_parent,
    [F_open] = (unixlib_entry_t)f_open,
    [F_close] = (unixlib_entry_t)f_close,
    [F_get_device] = (unixlib_entry_t)f_get_device,
    [F_get_configuration] = (unixlib_entry_t)f_get_configuration,
    [F_set_configuration] = (unixlib_entry_t)f_set_configuration,
    [F_claim_interface] = (unixlib_entry_t)f_claim_interface,
    [F_release_interface] = (unixlib_entry_t)f_release_interface,
    [F_set_interface_alt_setting] = (unixlib_entry_t)f_set_interface_alt_setting,
    [F_clear_halt] = (unixlib_entry_t)f_clear_halt,
    [F_reset_device] = (unixlib_entry_t)f_reset_device,
    [F_kernel_driver_active] = (unixlib_entry_t)f_kernel_driver_active,
    [F_detach_kernel_driver] = (unixlib_entry_t)f_detach_kernel_driver,
    [F_attach_kernel_driver] = (unixlib_entry_t)f_attach_kernel_driver,
    [F_set_auto_detach_kernel_driver] = (unixlib_entry_t)f_set_auto_detach_kernel_driver,
    [F_control_transfer] = (unixlib_entry_t)f_control_transfer,
    [F_bulk_transfer] = (unixlib_entry_t)f_bulk_transfer,
    [F_interrupt_transfer] = (unixlib_entry_t)f_interrupt_transfer,
    [F_get_string_descriptor_ascii] = (unixlib_entry_t)f_get_string_descriptor_ascii,
    [F_handle_events_timeout] = (unixlib_entry_t)f_handle_events_timeout,
    [F_handle_events_timeout_completed] = (unixlib_entry_t)f_handle_events_timeout_completed,
    [F_hotplug_register] = (unixlib_entry_t)f_hotplug_register,
    [F_hotplug_deregister] = (unixlib_entry_t)f_hotplug_deregister,
    [F_hotplug_pop] = (unixlib_entry_t)f_hotplug_pop,
    [F_error_name] = (unixlib_entry_t)f_error_name,
    [F_strerror] = (unixlib_entry_t)f_strerror,
    [F_get_version] = (unixlib_entry_t)f_get_version,
    [F_has_capability] = (unixlib_entry_t)f_has_capability,
    [F_open_device_with_vid_pid] = (unixlib_entry_t)f_open_device_with_vid_pid,
    [F_get_max_packet_size] = (unixlib_entry_t)f_get_max_packet_size,
    [F_get_device_string] = (unixlib_entry_t)f_get_device_string,
    [F_get_session_data] = (unixlib_entry_t)f_get_session_data,
    [F_get_config_descriptor_by_value] = (unixlib_entry_t)f_get_config_descriptor_by_value,
    [F_get_max_iso_packet_size] = (unixlib_entry_t)f_get_max_iso_packet_size,
    [F_get_max_alt_packet_size] = (unixlib_entry_t)f_get_max_alt_packet_size,
};
