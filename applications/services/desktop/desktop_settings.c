#include "desktop_settings.h"
#include "desktop_settings_filename.h"

#include <saved_struct.h>
#include <storage/storage.h>

#define TAG "DesktopSettings"

#define DESKTOP_SETTINGS_VER   (12)
#define DESKTOP_SETTINGS_MAGIC (0x13) // Different from OFW 0x17

void desktop_settings_load(DesktopSettings* settings) {
    furi_assert(settings);

    const bool success = saved_struct_load(
        DESKTOP_SETTINGS_PATH,
        settings,
        sizeof(DesktopSettings),
        DESKTOP_SETTINGS_MAGIC,
        DESKTOP_SETTINGS_VER);

    if(success) {
        // Cap at 24h (menu max is 1h): furi_timer_start asserts on ticks >= portMAX_DELAY.
        // Flag bytes are normalised to 0/1 so raw values behave as plain bools.
        if(settings->auto_lock_delay_ms > 24UL * 60 * 60 * 1000) {
            settings->auto_lock_delay_ms = 0;
        }
        settings->auto_lock_with_pin = !!settings->auto_lock_with_pin;
        settings->usb_inhibit_auto_lock = !!settings->usb_inhibit_auto_lock;
        settings->display_clock = !!settings->display_clock;
    }

    if(!success) {
        FURI_LOG_W(TAG, "Failed to load file, using defaults");
        memset(settings, 0, sizeof(DesktopSettings));
        // desktop_settings_save(settings);
    }
}

void desktop_settings_save(const DesktopSettings* settings) {
    furi_assert(settings);

    const bool success = saved_struct_save(
        DESKTOP_SETTINGS_PATH,
        settings,
        sizeof(DesktopSettings),
        DESKTOP_SETTINGS_MAGIC,
        DESKTOP_SETTINGS_VER);

    if(!success) {
        FURI_LOG_E(TAG, "Failed to save file");
    }
}
