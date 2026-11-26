// Methods for NFC transmission

// nfc
#include <furi.h>
#include <furi_hal.h>
#include <nfc/nfc.h>
#include <nfc/nfc_device.h>
#include <nfc/nfc_listener.h>

#include "action_i.h"
#include "quac.h"

void action_nfc_tx(void* context, const FuriString* action_path, FuriString* error) {
    App* app = context;

    FURI_LOG_I(TAG, "NFC: Tx %s", furi_string_get_cstr(action_path));
    Nfc* nfc = nfc_alloc();
    NfcDevice* device = nfc_device_alloc();

    if(nfc_device_load(device, furi_string_get_cstr(action_path))) {
        NfcProtocol protocol = nfc_device_get_protocol(device);
        FURI_LOG_I(TAG, "NFC: Protocol %s", nfc_device_get_protocol_name(protocol));

        // nfc_listener_alloc() furi_checks that the protocol has a listener; only these do
        switch(protocol) {
        case NfcProtocolIso14443_3a:
        case NfcProtocolIso14443_4a:
        case NfcProtocolIso15693_3:
        case NfcProtocolFelica:
        case NfcProtocolMfUltralight:
        case NfcProtocolMfClassic:
        case NfcProtocolSlix:
        case NfcProtocolType4Tag:
            break;
        default:
            FURI_LOG_E(TAG, "NFC: Emulation not supported for this protocol");
            ACTION_SET_ERROR("NFC: emulation not supported");
            nfc_device_clear(device);
            nfc_free(nfc);
            nfc_device_free(device);
            return;
        }

        NfcListener* listener =
            nfc_listener_alloc(nfc, protocol, nfc_device_get_data(device, protocol));
        FURI_LOG_I(TAG, "NFC: Starting...");
        nfc_listener_start(listener, NULL, NULL);

        int32_t time_ms = app->settings.nfc_duration;
        const int32_t interval_ms = 100;
        while(time_ms > 0) {
            furi_delay_ms(interval_ms);
            time_ms -= interval_ms;
        }

        FURI_LOG_I(TAG, "NFC: Done");
        nfc_listener_stop(listener);
        nfc_listener_free(listener);
    } else {
        FURI_LOG_E(TAG, "NFC: Failed to load %s", furi_string_get_cstr(action_path));
        ACTION_SET_ERROR("Failed to load %s", furi_string_get_cstr(action_path));
    }
    nfc_device_clear(device); // probably not needed?
    nfc_free(nfc);
    nfc_device_free(device);
}