#include "power_settings.h"
#include "power_settings_filename.h"

#include <saved_struct.h>
#include <storage/storage.h>

#define TAG "PowerSettings"

#define POWER_SETTINGS_VER_1 (1) // Oldest version
#define POWER_SETTINGS_VER_2 (2) // Charge-supress added
#define POWER_SETTINGS_VER_3 (3) // Percentage + warning timeout added
#define POWER_SETTINGS_VER   (4) // Critical hard-floor % added

#define POWER_SETTINGS_MAGIC (0x21)

// Longest delay the settings UI offers (48h); also keeps furi_ms_to_ticks < portMAX_DELAY
#define POWER_SETTINGS_DELAY_MS_MAX (172800000UL)

typedef struct {
    uint32_t auto_poweroff_delay_ms;
} PowerSettingsV1;

typedef struct {
    uint32_t auto_poweroff_delay_ms;
    uint8_t charge_supress_percent;
} PowerSettingsV2;

typedef struct {
    PowerAutoPoweroffMode auto_poweroff_mode;
    uint32_t auto_poweroff_delay_ms;
    uint8_t charge_supress_percent;
    uint8_t auto_poweroff_percent;
    PowerOffTimeout power_off_timeout;
} PowerSettingsV3;

// Clamp loaded values to what the settings UI can produce. The file checksum is
// weak and the file is writable over RPC, and power.c / the settings app use
// these raw (instant shutdown, OOB text-array reads, furi_timer_start assert).
static void power_settings_sanitize(PowerSettings* settings) {
    if((uint32_t)settings->auto_poweroff_mode > PowerAutoPoweroffModeTimerPercent) {
        settings->auto_poweroff_mode = PowerAutoPoweroffModeOff;
    }
    if(settings->auto_poweroff_delay_ms > POWER_SETTINGS_DELAY_MS_MAX) {
        settings->auto_poweroff_delay_ms = POWER_SETTINGS_DELAY_MS_MAX;
    }
    if(settings->charge_supress_percent > 95) { // UI maps 100% to 0 (off)
        settings->charge_supress_percent = 95;
    }
    if(settings->auto_poweroff_percent > 95) {
        settings->auto_poweroff_percent = 95;
    }
    if((uint32_t)settings->power_off_timeout > PowerOffTimeout90) {
        settings->power_off_timeout = PowerOffTimeout90;
    }
    if(settings->auto_poweroff_critical_percent > 5) {
        settings->auto_poweroff_critical_percent = 5;
    }
}

void power_settings_load(PowerSettings* settings) {
    furi_assert(settings);

    bool success = false;

    do {
        uint8_t version;
        if(!saved_struct_get_metadata(POWER_SETTINGS_PATH, NULL, &version, NULL)) break;

        // v3 - load it directly
        if(version == POWER_SETTINGS_VER) {
            success = saved_struct_load(
                POWER_SETTINGS_PATH,
                settings,
                sizeof(PowerSettings),
                POWER_SETTINGS_MAGIC,
                POWER_SETTINGS_VER);

            // v3 -> v4
        } else if(version == POWER_SETTINGS_VER_3) {
            PowerSettingsV3* prev = malloc(sizeof(PowerSettingsV3));
            success = saved_struct_load(
                POWER_SETTINGS_PATH,
                prev,
                sizeof(PowerSettingsV3),
                POWER_SETTINGS_MAGIC,
                POWER_SETTINGS_VER_3);
            if(success) {
                settings->auto_poweroff_mode = prev->auto_poweroff_mode;
                settings->auto_poweroff_delay_ms = prev->auto_poweroff_delay_ms;
                settings->charge_supress_percent = prev->charge_supress_percent;
                settings->auto_poweroff_percent = prev->auto_poweroff_percent;
                settings->power_off_timeout = prev->power_off_timeout;
                settings->auto_poweroff_critical_percent = 0;
            }
            free(prev);

            // v2 -> v4
        } else if(version == POWER_SETTINGS_VER_2) {
            PowerSettingsV2* prev = malloc(sizeof(PowerSettingsV2));
            success = saved_struct_load(
                POWER_SETTINGS_PATH,
                prev,
                sizeof(PowerSettingsV2),
                POWER_SETTINGS_MAGIC,
                POWER_SETTINGS_VER_2);
            if(success) {
                settings->auto_poweroff_mode = prev->auto_poweroff_delay_ms ?
                                                   PowerAutoPoweroffModeTimer :
                                                   PowerAutoPoweroffModeOff;
                settings->auto_poweroff_delay_ms = prev->auto_poweroff_delay_ms;
                settings->charge_supress_percent = prev->charge_supress_percent;
                settings->auto_poweroff_percent = 0;
                settings->power_off_timeout = PowerOffTimeout90;
                settings->auto_poweroff_critical_percent = 0;
            }
            free(prev);

            // v1 -> v4
        } else if(version == POWER_SETTINGS_VER_1) {
            PowerSettingsV1* prev = malloc(sizeof(PowerSettingsV1));
            success = saved_struct_load(
                POWER_SETTINGS_PATH,
                prev,
                sizeof(PowerSettingsV1),
                POWER_SETTINGS_MAGIC,
                POWER_SETTINGS_VER_1);
            if(success) {
                settings->auto_poweroff_mode = prev->auto_poweroff_delay_ms ?
                                                   PowerAutoPoweroffModeTimer :
                                                   PowerAutoPoweroffModeOff;
                settings->auto_poweroff_delay_ms = prev->auto_poweroff_delay_ms;
                settings->charge_supress_percent = 0;
                settings->auto_poweroff_percent = 0;
                settings->power_off_timeout = PowerOffTimeout90;
                settings->auto_poweroff_critical_percent = 0;
            }
            free(prev);
        }

    } while(false);

    if(success) {
        power_settings_sanitize(settings);
    }

    if(!success) {
        FURI_LOG_W(TAG, "Failed to load file, using defaults");
        memset(settings, 0, sizeof(PowerSettings));
        settings->power_off_timeout = PowerOffTimeout90;
    }
}

void power_settings_save(const PowerSettings* settings) {
    furi_assert(settings);

    const bool success = saved_struct_save(
        POWER_SETTINGS_PATH,
        settings,
        sizeof(PowerSettings),
        POWER_SETTINGS_MAGIC,
        POWER_SETTINGS_VER);

    if(!success) {
        FURI_LOG_E(TAG, "Failed to save file");
    }
}
