#pragma once

#include <furi_hal.h>
#include <flipper_application/flipper_application.h>

#define RX_BUF_SIZE 1024

typedef struct SubGhzGPS SubGhzGPS;

struct SubGhzGPS {
    FlipperApplication* plugin_app;
    FuriThread* thread;
    FuriStreamBuffer* rx_stream;
    uint8_t rx_buf[RX_BUF_SIZE];
    FuriHalSerialHandle* serial_handle;
    FuriTimer* timer;

    float latitude;
    float longitude;
    int satellites;
    uint8_t fix_second;
    uint8_t fix_minute;
    uint8_t fix_hour;

    /**
     * Deinitialize SubGhzGPS object
     * To be used by plugin handler
     * 
     * @param subghz_gps SubGhzGPS object
     * @return void
    */
    void (*deinit)(SubGhzGPS* subghz_gps);

    /**
     * Concatenate realtime GPS info to string
     * 
     * @param subghz_gps SubGhzGPS object
     * @param descr Output string
     * @param latitude Latitude
     * @param longitude Longitude
     * @return void
    */
    void (*cat_realtime)(SubGhzGPS* subghz_gps, FuriString* descr, float latitude, float longitude);

    // Appended last so earlier member offsets stay unchanged for the plugin
    uint32_t fix_tick; // furi_get_tick() of the last parsed sentence, 0 = no fix yet
};

#define SUBGHZ_GPS_FIX_TIMEOUT_S 15U

/**
 * Check whether the last GPS fix is older than SUBGHZ_GPS_FIX_TIMEOUT_S
 * Uses the local tick counter: GPS UTC and the RTC are unrelated clocks and can't be compared
 *
 * @param subghz_gps SubGhzGPS object
 * @return true if a fix was received before and it has gone stale
*/
static inline bool subghz_gps_fix_is_stale(const SubGhzGPS* subghz_gps) {
    if(subghz_gps->fix_tick == 0) return false;
    // unsigned delta stays correct across tick counter wraparound
    return (furi_get_tick() - subghz_gps->fix_tick) >
           (SUBGHZ_GPS_FIX_TIMEOUT_S * furi_kernel_get_tick_frequency());
}

/**
 * Initialize SubGhzGPS plugin
 * Fails and returns NULL if Expansion is connected
 *
 * @return SubGhzGPS* SubGhzGPS object
*/
SubGhzGPS* subghz_gps_plugin_init(uint32_t baudrate);

/**
 * Deinitialize SubGhzGPS plugin
 * 
 * @param subghz_gps SubGhzGPS object
 * @return void
*/
void subghz_gps_plugin_deinit(SubGhzGPS* subghz_gps);
