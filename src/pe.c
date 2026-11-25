/* PE half: a drop-in libusb.dll that forwards to the Unix half (artwine.so),
 * which must sit in the same directory as this DLL. */
#include <windows.h>
#include <winternl.h>
#include <stdarg.h>
#include <wchar.h>
#include "common.h"

typedef UINT64 unixlib_handle_t;
extern NTSTATUS (WINAPI *__wine_unix_call_dispatcher)(unixlib_handle_t, unsigned int, void *);
extern NTSTATUS WINAPI __wine_load_unix_lib(const UNICODE_STRING *name, UINT64 *lib, unixlib_handle_t *handle);

#define EXPORT __declspec(dllexport)
#define LIBUSB_ERROR_NOT_FOUND (-5)
#define LIBUSB_ERROR_NO_MEM (-11)
#define LIBUSB_ERROR_NOT_SUPPORTED (-12)
#define HOTPLUG_DEREGISTER 1

static unixlib_handle_t unix_handle;
static BOOL unix_loaded;
static INIT_ONCE load_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK load_unix(INIT_ONCE *once, void *param, void **ctx)
{
    WCHAR path[MAX_PATH], nt[MAX_PATH + 8];
    HMODULE self;
    UNICODE_STRING name;
    UINT64 lib;
    WCHAR *slash;

    (void)once; (void)param; (void)ctx;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)load_unix, &self))
        return TRUE;
    if (!GetModuleFileNameW(self, path, MAX_PATH - 32)) return TRUE;
    if (!(slash = wcsrchr(path, L'\\'))) return TRUE;
    wcscpy(slash + 1, L"artwine.so");
    wcscpy(nt, L"\\??\\");
    wcscat(nt, path);
    RtlInitUnicodeString(&name, nt);
    unix_loaded = !__wine_load_unix_lib(&name, &lib, &unix_handle);
    if (!unix_loaded) OutputDebugStringW(L"artwine-bridge: failed to load artwine.so\n");
    return TRUE;
}

static INT64 call(unsigned id, struct bridge_args *a)
{
    InitOnceExecuteOnce(&load_once, load_unix, NULL, NULL);
    if (!unix_loaded) return LIBUSB_ERROR_NOT_SUPPORTED;
    __wine_unix_call_dispatcher(unix_handle, id, a);
    return a->ret;
}

#define A(x) ((UINT64)(UINT_PTR)(x))
#define CALL(id, ...) ({ struct bridge_args _a = { { __VA_ARGS__ }, 0 }; call(id, &_a); })

/* ---- hotplug: emulated on the PE side so callbacks run in Windows code ---- */
typedef int (WINAPI *hotplug_cb)(void *ctx, void *dev, int event, void *user);
#define MAX_HOTPLUG 64
static struct {
    BOOL used;
    int native;
    void *ctx;
    hotplug_cb cb;
    void *user;
} hp[MAX_HOTPLUG];
static CRITICAL_SECTION hp_cs;
static DWORD hp_tls = TLS_OUT_OF_INDEXES;

static void hotplug_drain(void)
{
    int slot, event;
    void *dev;

    if (hp_tls == TLS_OUT_OF_INDEXES || TlsGetValue(hp_tls)) return; /* no re-entry */
    TlsSetValue(hp_tls, (void *)1);
    while (CALL(F_hotplug_pop, A(&slot), A(&event), A(&dev)) == 1) {
        hotplug_cb cb = NULL;
        void *ctx = NULL, *user = NULL;
        EnterCriticalSection(&hp_cs);
        if (slot >= 0 && slot < MAX_HOTPLUG && hp[slot].used) {
            cb = hp[slot].cb; ctx = hp[slot].ctx; user = hp[slot].user;
        }
        LeaveCriticalSection(&hp_cs);
        if (cb && cb(ctx, dev, event, user) == HOTPLUG_DEREGISTER) {
            EnterCriticalSection(&hp_cs);
            if (hp[slot].used) {
                CALL(F_hotplug_deregister, A(hp[slot].ctx), hp[slot].native);
                hp[slot].used = FALSE;
            }
            LeaveCriticalSection(&hp_cs);
        }
        CALL(F_unref_device, A(dev));
    }
    TlsSetValue(hp_tls, NULL);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&hp_cs);
        hp_tls = TlsAlloc();
    }
    return TRUE;
}

/* ---- exported libusb API ---- */
EXPORT int libusb_init_context(void **ctx, const void *options, int num_options)
{
    (void)options; (void)num_options;
    return (int)CALL(F_init_context, A(ctx));
}
EXPORT int libusb_init(void **ctx) { return (int)CALL(F_init_context, A(ctx)); }
EXPORT void libusb_exit(void *ctx) { CALL(F_exit, A(ctx)); }
EXPORT void libusb_set_debug(void *ctx, int level) { CALL(F_set_debug, A(ctx), level); }
EXPORT int libusb_set_option(void *ctx, int option, ...)
{
    if (option == 0) { /* LIBUSB_OPTION_LOG_LEVEL */
        va_list ap;
        va_start(ap, option);
        int level = va_arg(ap, int);
        va_end(ap);
        CALL(F_set_debug, A(ctx), level);
    }
    return 0;
}
EXPORT INT_PTR libusb_get_device_list(void *ctx, void ***list) { return (INT_PTR)CALL(F_get_device_list, A(ctx), A(list)); }
EXPORT void libusb_free_device_list(void **list, int unref) { CALL(F_free_device_list, A(list), unref); }
EXPORT void *libusb_ref_device(void *dev) { return (void *)(UINT_PTR)CALL(F_ref_device, A(dev)); }
EXPORT void libusb_unref_device(void *dev) { CALL(F_unref_device, A(dev)); }
EXPORT int libusb_get_device_descriptor(void *dev, void *desc) { return (int)CALL(F_get_device_descriptor, A(dev), A(desc)); }
EXPORT int libusb_get_active_config_descriptor(void *dev, void **config)
{
    return (int)CALL(F_get_active_config_descriptor, A(dev), A(config));
}
EXPORT int libusb_get_config_descriptor(void *dev, UINT8 index, void **config)
{
    return (int)CALL(F_get_config_descriptor, A(dev), index, A(config));
}
EXPORT void libusb_free_config_descriptor(void *config) { CALL(F_free_config_descriptor, A(config)); }
EXPORT UINT8 libusb_get_bus_number(void *dev) { return (UINT8)CALL(F_get_bus_number, A(dev)); }
EXPORT UINT8 libusb_get_device_address(void *dev) { return (UINT8)CALL(F_get_device_address, A(dev)); }
EXPORT int libusb_get_device_speed(void *dev) { return (int)CALL(F_get_device_speed, A(dev)); }
EXPORT UINT8 libusb_get_port_number(void *dev) { return (UINT8)CALL(F_get_port_number, A(dev)); }
EXPORT int libusb_get_port_numbers(void *dev, UINT8 *ports, int len) { return (int)CALL(F_get_port_numbers, A(dev), A(ports), len); }
EXPORT int libusb_get_port_path(void *ctx, void *dev, UINT8 *path, UINT8 len)
{
    return (int)CALL(F_get_port_path, A(ctx), A(dev), A(path), len);
}
EXPORT void *libusb_get_parent(void *dev) { return (void *)(UINT_PTR)CALL(F_get_parent, A(dev)); }
EXPORT int libusb_open(void *dev, void **handle) { return (int)CALL(F_open, A(dev), A(handle)); }
EXPORT void libusb_close(void *handle) { CALL(F_close, A(handle)); }
EXPORT void *libusb_get_device(void *handle) { return (void *)(UINT_PTR)CALL(F_get_device, A(handle)); }
EXPORT int libusb_get_configuration(void *handle, int *config) { return (int)CALL(F_get_configuration, A(handle), A(config)); }
EXPORT int libusb_set_configuration(void *handle, int config) { return (int)CALL(F_set_configuration, A(handle), config); }
EXPORT int libusb_claim_interface(void *handle, int iface) { return (int)CALL(F_claim_interface, A(handle), iface); }
EXPORT int libusb_release_interface(void *handle, int iface) { return (int)CALL(F_release_interface, A(handle), iface); }
EXPORT int libusb_set_interface_alt_setting(void *handle, int iface, int alt)
{
    return (int)CALL(F_set_interface_alt_setting, A(handle), iface, alt);
}
EXPORT int libusb_clear_halt(void *handle, unsigned char ep) { return (int)CALL(F_clear_halt, A(handle), ep); }
EXPORT int libusb_reset_device(void *handle) { return (int)CALL(F_reset_device, A(handle)); }
EXPORT int libusb_kernel_driver_active(void *handle, int iface) { return (int)CALL(F_kernel_driver_active, A(handle), iface); }
EXPORT int libusb_detach_kernel_driver(void *handle, int iface) { return (int)CALL(F_detach_kernel_driver, A(handle), iface); }
EXPORT int libusb_attach_kernel_driver(void *handle, int iface) { return (int)CALL(F_attach_kernel_driver, A(handle), iface); }
EXPORT int libusb_set_auto_detach_kernel_driver(void *handle, int enable)
{
    return (int)CALL(F_set_auto_detach_kernel_driver, A(handle), enable);
}
EXPORT int libusb_control_transfer(void *handle, UINT8 type, UINT8 req, UINT16 value, UINT16 index,
                                   unsigned char *data, UINT16 len, unsigned int timeout)
{
    return (int)CALL(F_control_transfer, A(handle), type, req, value, index, A(data), len, timeout);
}
EXPORT int libusb_bulk_transfer(void *handle, unsigned char ep, unsigned char *data, int len,
                                int *transferred, unsigned int timeout)
{
    return (int)CALL(F_bulk_transfer, A(handle), ep, A(data), (UINT64)(INT64)len, A(transferred), timeout);
}
EXPORT int libusb_interrupt_transfer(void *handle, unsigned char ep, unsigned char *data, int len,
                                     int *transferred, unsigned int timeout)
{
    return (int)CALL(F_interrupt_transfer, A(handle), ep, A(data), (UINT64)(INT64)len, A(transferred), timeout);
}
EXPORT int libusb_get_string_descriptor_ascii(void *handle, UINT8 index, unsigned char *data, int len)
{
    return (int)CALL(F_get_string_descriptor_ascii, A(handle), index, A(data), (UINT64)(INT64)len);
}

/* Windows struct timeval has 32-bit longs. */
struct win_timeval { LONG tv_sec; LONG tv_usec; };

EXPORT int libusb_handle_events_timeout(void *ctx, struct win_timeval *tv)
{
    INT64 r = CALL(F_handle_events_timeout, A(ctx), (UINT64)(INT64)(tv ? tv->tv_sec : 0),
                   (UINT64)(INT64)(tv ? tv->tv_usec : 0));
    hotplug_drain();
    return (int)r;
}
EXPORT int libusb_handle_events_timeout_completed(void *ctx, struct win_timeval *tv, int *completed)
{
    INT64 r = CALL(F_handle_events_timeout_completed, A(ctx), (UINT64)(INT64)(tv ? tv->tv_sec : 0),
                   (UINT64)(INT64)(tv ? tv->tv_usec : 0), A(completed));
    hotplug_drain();
    return (int)r;
}
EXPORT int libusb_handle_events(void *ctx)
{
    struct win_timeval tv = { 60, 0 };
    return libusb_handle_events_timeout(ctx, &tv);
}
EXPORT int libusb_handle_events_completed(void *ctx, int *completed)
{
    struct win_timeval tv = { 60, 0 };
    return libusb_handle_events_timeout_completed(ctx, &tv, completed);
}

EXPORT int libusb_hotplug_register_callback(void *ctx, int events, int flags, int vendor_id, int product_id,
                                            int dev_class, hotplug_cb cb, void *user, int *handle)
{
    int slot, native = 0;
    INT64 r;

    EnterCriticalSection(&hp_cs);
    for (slot = 0; slot < MAX_HOTPLUG && hp[slot].used; slot++);
    if (slot == MAX_HOTPLUG) { LeaveCriticalSection(&hp_cs); return LIBUSB_ERROR_NO_MEM; }
    hp[slot].used = TRUE;
    hp[slot].ctx = ctx;
    hp[slot].cb = cb;
    hp[slot].user = user;
    LeaveCriticalSection(&hp_cs);

    r = CALL(F_hotplug_register, A(ctx), events, flags, (UINT64)(INT64)vendor_id, (UINT64)(INT64)product_id,
             (UINT64)(INT64)dev_class, slot, A(&native));
    EnterCriticalSection(&hp_cs);
    if (r) hp[slot].used = FALSE;
    else hp[slot].native = native;
    LeaveCriticalSection(&hp_cs);
    if (r) return (int)r;
    if (handle) *handle = slot + 1;
    hotplug_drain(); /* deliver LIBUSB_HOTPLUG_ENUMERATE arrivals now, like libusb does */
    return 0;
}
EXPORT void libusb_hotplug_deregister_callback(void *ctx, int handle)
{
    int slot = handle - 1;
    (void)ctx;
    if (slot < 0 || slot >= MAX_HOTPLUG) return;
    EnterCriticalSection(&hp_cs);
    if (hp[slot].used) {
        CALL(F_hotplug_deregister, A(hp[slot].ctx), hp[slot].native);
        hp[slot].used = FALSE;
    }
    LeaveCriticalSection(&hp_cs);
}
EXPORT void *libusb_hotplug_get_user_data(void *ctx, int handle)
{
    int slot = handle - 1;
    (void)ctx;
    return (slot >= 0 && slot < MAX_HOTPLUG && hp[slot].used) ? hp[slot].user : NULL;
}

EXPORT const char *libusb_error_name(int code) { return (const char *)(UINT_PTR)CALL(F_error_name, (UINT64)(INT64)code); }
EXPORT const char *libusb_strerror(int code) { return (const char *)(UINT_PTR)CALL(F_strerror, (UINT64)(INT64)code); }
EXPORT const void *libusb_get_version(void) { return (const void *)(UINT_PTR)CALL(F_get_version, 0); }
EXPORT int libusb_has_capability(UINT32 cap) { return (int)CALL(F_has_capability, cap); }
EXPORT int libusb_setlocale(const char *locale) { (void)locale; return 0; }
EXPORT void *libusb_open_device_with_vid_pid(void *ctx, UINT16 vid, UINT16 pid)
{
    return (void *)(UINT_PTR)CALL(F_open_device_with_vid_pid, A(ctx), vid, pid);
}
EXPORT int libusb_get_max_packet_size(void *dev, unsigned char ep) { return (int)CALL(F_get_max_packet_size, A(dev), ep); }

EXPORT int libusb_get_device_string(void *dev, int type, char *data, int len)
{
    return (int)CALL(F_get_device_string, A(dev), (UINT64)(INT64)type, A(data), (UINT64)(INT64)len);
}
/* Windows unsigned long is 32-bit */
EXPORT ULONG libusb_get_session_data(void *dev) { return (ULONG)CALL(F_get_session_data, A(dev)); }
EXPORT int libusb_get_config_descriptor_by_value(void *dev, UINT8 value, void **config)
{
    return (int)CALL(F_get_config_descriptor_by_value, A(dev), value, A(config));
}
EXPORT int libusb_get_max_iso_packet_size(void *dev, unsigned char ep) { return (int)CALL(F_get_max_iso_packet_size, A(dev), ep); }
EXPORT int libusb_get_max_alt_packet_size(void *dev, int iface, int alt, unsigned char ep)
{
    return (int)CALL(F_get_max_alt_packet_size, A(dev), (UINT64)(INT64)iface, (UINT64)(INT64)alt, ep);
}
