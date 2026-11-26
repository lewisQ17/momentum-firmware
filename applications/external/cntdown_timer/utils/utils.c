#include <furi.h>
#include "utils.h"

static const NotificationSequence sequence_beep = {
    &message_blue_255,
    &message_note_d5,
    &message_delay_100,
    &message_sound_off,

    NULL,
};

static const NotificationSequence sequence_timeup = {
    &message_force_display_brightness_setting_1f,
    &message_display_backlight_on,
    &message_vibro_on,

    &message_note_c8,
    &message_delay_50,
    &message_sound_off,
    &message_delay_50,
    &message_delay_25,

    &message_note_c8,
    &message_delay_50,
    &message_sound_off,
    &message_delay_50,
    &message_delay_25,

    &message_note_c8,
    &message_delay_50,
    &message_sound_off,
    &message_delay_50,
    &message_delay_25,

    &message_note_c8,
    &message_delay_50,
    &message_sound_off,
    &message_delay_50,
    &message_delay_25,

    &message_vibro_off,
    &message_display_backlight_off,
    &message_delay_500,

    NULL,
};

// every open is closed in the same function; the old unbalanced open/close pair drifted the holder count
void notification_beep_once() {
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, &sequence_beep);
    furi_record_close(RECORD_NOTIFICATION);
}

void notification_off() {
    // nothing is held between calls any more; kept for the callers
}

void notification_timeup() {
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, &sequence_timeup);
    furi_record_close(RECORD_NOTIFICATION);
}

void parse_sec_to_time_str(char* buffer, size_t len, int32_t sec) {
    snprintf(
        buffer,
        len,
        "%02ld:%02ld:%02ld",
        sec / (60 * 60), // hour (no 24 h wrap: the timer can be set above 24 h)
        (sec % (60 * 60)) / 60, // minute
        sec % 60); // second
}
