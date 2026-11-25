/* Shared between the PE half (libusb.dll) and the Unix half (artwine.so).
 * Both sides are x86_64 in one address space, so pointers pass through as-is. */
#ifndef BRIDGE_COMMON_H
#define BRIDGE_COMMON_H

#include <stdint.h>

struct bridge_args {
    uint64_t a[10];
    int64_t ret;
};

enum bridge_func {
    F_init_context,
    F_exit,
    F_set_debug,
    F_get_device_list,
    F_free_device_list,
    F_ref_device,
    F_unref_device,
    F_get_device_descriptor,
    F_get_active_config_descriptor,
    F_get_config_descriptor,
    F_free_config_descriptor,
    F_get_bus_number,
    F_get_device_address,
    F_get_device_speed,
    F_get_port_number,
    F_get_port_numbers,
    F_get_port_path,
    F_get_parent,
    F_open,
    F_close,
    F_get_device,
    F_get_configuration,
    F_set_configuration,
    F_claim_interface,
    F_release_interface,
    F_set_interface_alt_setting,
    F_clear_halt,
    F_reset_device,
    F_kernel_driver_active,
    F_detach_kernel_driver,
    F_attach_kernel_driver,
    F_set_auto_detach_kernel_driver,
    F_control_transfer,
    F_bulk_transfer,
    F_interrupt_transfer,
    F_get_string_descriptor_ascii,
    F_handle_events_timeout,
    F_handle_events_timeout_completed,
    F_hotplug_register,
    F_hotplug_deregister,
    F_hotplug_pop,
    F_error_name,
    F_strerror,
    F_get_version,
    F_has_capability,
    F_open_device_with_vid_pid,
    F_get_max_packet_size,
    F_get_device_string,
    F_get_session_data,
    F_get_config_descriptor_by_value,
    F_get_max_iso_packet_size,
    F_get_max_alt_packet_size,
    F_COUNT
};

#endif
