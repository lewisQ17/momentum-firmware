#include "scheduler_run.h"

#include <furi.h>
#include <furi_hal.h>

#include "helpers/subghz_txrx.h"
#include <lib/subghz/blocks/custom_btn.h>
#include <lib/subghz/protocols/raw.h>

#include <input/input.h>

#define TAG "SubGhzSchedulerRun"

struct ScheduleTxRun {
    Storage* storage;
    FlipperFormat* fff_head;
    FlipperFormat* fff_data;
    FuriString* data;
    bool filetype;
    uint16_t tx_delay;
    bool raw_file_is_tx;
};

static ScheduleTxRun* tx_run_alloc() {
    ScheduleTxRun* tx_run = malloc(sizeof(ScheduleTxRun));
    tx_run->storage = furi_record_open(RECORD_STORAGE);
    tx_run->fff_head = flipper_format_file_alloc(tx_run->storage);
    tx_run->data = furi_string_alloc();
    return tx_run;
}

static void tx_run_free(ScheduleTxRun* tx_run) {
    flipper_format_file_close(tx_run->fff_head);
    flipper_format_free(tx_run->fff_head);
    furi_string_free(tx_run->data);
    furi_record_close(RECORD_STORAGE);
    free(tx_run);
}

typedef struct SubGhzNeedSaveContext {
    ScheduleTxRun* tx_run;
    SubGhzTxRx* txrx;
    char* file_path;
} SubGhzNeedSaveContext;

static void scheduler_subghz_need_save_callback(void* context) {
    FURI_LOG_I(TAG, "Saving updated subghz signal");
    SubGhzNeedSaveContext* savectx = (SubGhzNeedSaveContext*)context;
    FlipperFormat* ff = subghz_txrx_get_fff_data(savectx->txrx);

    Stream* ff_stream = flipper_format_get_raw_stream(ff);
    flipper_format_delete_key(ff, "Repeat");

    FuriString* tmp_path = furi_string_alloc_printf("%s.tmp", savectx->file_path);
    do {
        // write to a temp file first, so the original capture survives a failed save
        stream_seek(ff_stream, 0, StreamOffsetFromStart);
        if(stream_save_to_file(
               ff_stream,
               savectx->tx_run->storage,
               furi_string_get_cstr(tmp_path),
               FSOM_CREATE_ALWAYS) == 0) {
            FURI_LOG_E(TAG, "Failed to write temporary subghz file");
            storage_simply_remove(savectx->tx_run->storage, furi_string_get_cstr(tmp_path));
            break;
        }
        if(!storage_simply_remove(savectx->tx_run->storage, savectx->file_path)) {
            FURI_LOG_E(TAG, "Failed to delete subghz file before re-save");
            storage_simply_remove(savectx->tx_run->storage, furi_string_get_cstr(tmp_path));
            break;
        }
        if(storage_common_rename(
               savectx->tx_run->storage, furi_string_get_cstr(tmp_path), savectx->file_path) !=
           FSE_OK) {
            FURI_LOG_E(TAG, "Failed to move re-saved subghz file into place");
            break;
        }
        if(storage_common_stat(savectx->tx_run->storage, savectx->file_path, NULL) != FSE_OK) {
            FURI_LOG_E(TAG, "Error verifying new subghz file after re-save");
            break;
        }
    } while(0);
    furi_string_free(tmp_path);
}

static void scheduler_subghz_raw_end_callback(void* context) {
    FURI_LOG_I(TAG, "Stopping TX on RAW");
    furi_assert(context);
    ScheduleTxRun* tx_run = context;

    tx_run->raw_file_is_tx = false;
}

// Delay that returns early once the app asked the TX thread to stop
static void scheduler_delay_interruptible(SchedulerApp* app, uint32_t ms) {
    while(ms > 0 && !app->stop_tx) {
        uint32_t step = ms > 20 ? 20 : ms;
        furi_delay_ms(step);
        ms -= step;
    }
}

static int32_t scheduler_tx(void* context) {
    SchedulerApp* app = context;
    ScheduleTxRun* tx_run = tx_run_alloc();
    SubGhzTxRx* txrx = subghz_txrx_alloc();

    FuriString* preset_name = furi_string_alloc();
    FuriString* protocol_name = furi_string_alloc();
    FuriString* temp_str = furi_string_alloc();

    tx_run->filetype = scheduler_get_file_type(app->scheduler);
    if(tx_run->filetype == SchedulerFileTypePlaylist) {
        flipper_format_file_open_existing(tx_run->fff_head, furi_string_get_cstr(app->file_path));
        flipper_format_read_string(tx_run->fff_head, "sub", tx_run->data);
    } else {
        furi_string_set_str(tx_run->data, furi_string_get_cstr(app->file_path));
    }

    tx_run->tx_delay = scheduler_get_tx_delay(app->scheduler);

    do {
        const char* file_name = furi_string_get_cstr(tx_run->data);

        FlipperFormat* fff_data_file = flipper_format_file_alloc(tx_run->storage);

        SubGhzNeedSaveContext save_context = {tx_run, txrx, (char*)file_name};
        subghz_txrx_set_need_save_callback(
            txrx, scheduler_subghz_need_save_callback, &save_context);

        Stream* fff_data_stream = flipper_format_get_raw_stream(subghz_txrx_get_fff_data(txrx));
        stream_clean(fff_data_stream);

        furi_string_reset(temp_str);
        furi_string_reset(preset_name);
        furi_string_reset(protocol_name);

        tx_run->raw_file_is_tx = false;

        subghz_custom_btns_reset();

        uint32_t temp_data32;

        uint32_t frequency = 0;

        if(!flipper_format_file_open_existing(fff_data_file, file_name)) {
            FURI_LOG_E(TAG, "Error opening %s", file_name);
            flipper_format_free(fff_data_file);
            subghz_txrx_free(txrx);
            furi_string_free(preset_name);
            furi_string_free(protocol_name);
            furi_string_free(temp_str);
            tx_run_free(tx_run);
            return -1;
        }

        if(!flipper_format_read_header(fff_data_file, temp_str, &temp_data32)) {
            FURI_LOG_E(TAG, "Missing or incorrect header");
            flipper_format_file_close(fff_data_file);
            flipper_format_free(fff_data_file);
            subghz_txrx_free(txrx);
            furi_string_free(preset_name);
            furi_string_free(protocol_name);
            furi_string_free(temp_str);
            tx_run_free(tx_run);
            return -1;
        }

        if(((!strcmp(furi_string_get_cstr(temp_str), SUBGHZ_KEY_FILE_TYPE)) ||
            (!strcmp(furi_string_get_cstr(temp_str), SUBGHZ_RAW_FILE_TYPE))) &&
           temp_data32 == SUBGHZ_KEY_FILE_VERSION) {
        } else {
            FURI_LOG_E(TAG, "Type or version mismatch");
            flipper_format_file_close(fff_data_file);
            flipper_format_free(fff_data_file);
            subghz_txrx_free(txrx);
            furi_string_free(preset_name);
            furi_string_free(protocol_name);
            furi_string_free(temp_str);
            tx_run_free(tx_run);
            return -1;
        }

        SubGhzSetting* setting = subghz_txrx_get_setting(txrx);
        if(!flipper_format_read_uint32(fff_data_file, "Frequency", &frequency, 1)) {
            FURI_LOG_W(TAG, "Missing Frequency. Setting default frequency");
            frequency = subghz_setting_get_default_frequency(setting);
        } else if(!subghz_txrx_radio_device_is_frequecy_valid(txrx, frequency)) {
            FURI_LOG_E(TAG, "Frequency not supported on the chosen radio module");
            flipper_format_file_close(fff_data_file);
            flipper_format_free(fff_data_file);
            subghz_txrx_free(txrx);
            furi_string_free(preset_name);
            furi_string_free(protocol_name);
            furi_string_free(temp_str);
            tx_run_free(tx_run);
            return -1;
        }

        if(!flipper_format_read_string(fff_data_file, "Preset", temp_str)) {
            FURI_LOG_E(TAG, "Missing Preset");
            flipper_format_file_close(fff_data_file);
            flipper_format_free(fff_data_file);
            subghz_txrx_free(txrx);
            furi_string_free(preset_name);
            furi_string_free(protocol_name);
            furi_string_free(temp_str);
            tx_run_free(tx_run);
            return -1;
        }

        furi_string_set_str(
            temp_str, subghz_txrx_get_preset_name(txrx, furi_string_get_cstr(temp_str)));
        if(!strcmp(furi_string_get_cstr(temp_str), "")) {
            FURI_LOG_E(TAG, "Unknown preset");
            flipper_format_file_close(fff_data_file);
            flipper_format_free(fff_data_file);
            subghz_txrx_free(txrx);
            furi_string_free(preset_name);
            furi_string_free(protocol_name);
            furi_string_free(temp_str);
            tx_run_free(tx_run);
            return -1;
        }

        if(!strcmp(furi_string_get_cstr(temp_str), "CUSTOM")) {
            subghz_setting_delete_custom_preset(setting, furi_string_get_cstr(temp_str));
            if(!subghz_setting_load_custom_preset(
                   setting, furi_string_get_cstr(temp_str), fff_data_file)) {
                FURI_LOG_E(TAG, "Missing Custom preset");
                flipper_format_file_close(fff_data_file);
                flipper_format_free(fff_data_file);
                subghz_txrx_free(txrx);
                furi_string_free(preset_name);
                furi_string_free(protocol_name);
                furi_string_free(temp_str);
                tx_run_free(tx_run);
                return -1;
            }
        }
        furi_string_set(preset_name, temp_str);
        size_t preset_index =
            subghz_setting_get_inx_preset_by_name(setting, furi_string_get_cstr(preset_name));
        subghz_txrx_set_preset(
            txrx,
            furi_string_get_cstr(preset_name),
            frequency,
            subghz_setting_get_preset_data(setting, preset_index),
            subghz_setting_get_preset_data_size(setting, preset_index));

        // Load Protocol
        if(!flipper_format_read_string(fff_data_file, "Protocol", protocol_name)) {
            FURI_LOG_E(TAG, "Missing protocol");
            flipper_format_file_close(fff_data_file);
            flipper_format_free(fff_data_file);
            subghz_txrx_free(txrx);
            furi_string_free(preset_name);
            furi_string_free(protocol_name);
            furi_string_free(temp_str);
            tx_run_free(tx_run);
            return -1;
        }

        FlipperFormat* fff_data = subghz_txrx_get_fff_data(txrx);
        if(!strcmp(furi_string_get_cstr(protocol_name), "RAW")) {
            subghz_protocol_raw_gen_fff_data(
                fff_data, file_name, subghz_txrx_radio_device_get_name(txrx));
        } else {
            stream_copy_full(
                flipper_format_get_raw_stream(fff_data_file),
                flipper_format_get_raw_stream(fff_data));
        }

        uint32_t repeat = 200;
        if(!flipper_format_insert_or_update_uint32(fff_data, "Repeat", &repeat, 1)) {
            FURI_LOG_E(TAG, "Unable Repeat");
            //
        }

        if(subghz_txrx_load_decoder_by_name_protocol(txrx, furi_string_get_cstr(protocol_name))) {
            SubGhzProtocolStatus status =
                subghz_protocol_decoder_base_deserialize(subghz_txrx_get_decoder(txrx), fff_data);
            if(status != SubGhzProtocolStatusOk) {
                flipper_format_file_close(fff_data_file);
                flipper_format_free(fff_data_file);
                subghz_txrx_free(txrx);
                furi_string_free(preset_name);
                furi_string_free(protocol_name);
                furi_string_free(temp_str);
                tx_run_free(tx_run);
                return -1;
            }
        } else {
            FURI_LOG_E(TAG, "Protocol not found: %s", furi_string_get_cstr(protocol_name));
            flipper_format_file_close(fff_data_file);
            flipper_format_free(fff_data_file);
            subghz_txrx_free(txrx);
            furi_string_free(preset_name);
            furi_string_free(protocol_name);
            furi_string_free(temp_str);
            tx_run_free(tx_run);
            return -1;
        }

        flipper_format_file_close(fff_data_file);
        flipper_format_free(fff_data_file);
        uint8_t repeats = scheduler_get_tx_repeats(app->scheduler);
        for(uint16_t i = 0; i <= repeats && !app->stop_tx; ++i) {
            notification_message(app->notifications, &sequence_blink_stop);
            notification_message(app->notifications, &sequence_blink_start_cyan);

            bool tx_started = true;
            if(subghz_txrx_tx_start(txrx, subghz_txrx_get_fff_data(txrx)) !=
               SubGhzTxRxStartTxStateOk) {
                FURI_LOG_E(TAG, "Failed to start TX");
                tx_started = false;
            }

            bool skip_extra_stop = false;
            FURI_LOG_D(TAG, "Checking if file is RAW...");
            if(tx_started && !strcmp(furi_string_get_cstr(protocol_name), "RAW")) {
                subghz_txrx_set_raw_file_encoder_worker_callback_end(
                    txrx, scheduler_subghz_raw_end_callback, tx_run);
                tx_run->raw_file_is_tx = true;
                skip_extra_stop = true;
            }
            do {
                furi_delay_ms(1);
            } while(tx_run->raw_file_is_tx && !app->stop_tx);

            notification_message(app->notifications, &sequence_blink_stop);

            if(!tx_run->raw_file_is_tx && !skip_extra_stop) {
                scheduler_delay_interruptible(app, 120);
                scheduler_delay_interruptible(app, tx_run->tx_delay);
                subghz_txrx_stop(txrx);
            } else {
                scheduler_delay_interruptible(app, 50);
                scheduler_delay_interruptible(app, tx_run->tx_delay);
                subghz_txrx_stop(txrx);
            }
            skip_extra_stop = false;
        }

    } while(tx_run->filetype == SchedulerFileTypePlaylist && !app->stop_tx &&
            flipper_format_read_string(tx_run->fff_head, "sub", tx_run->data));

    subghz_custom_btns_reset();
    tx_run_free(tx_run);
    subghz_txrx_free(txrx);

    furi_string_free(preset_name);
    furi_string_free(protocol_name);
    furi_string_free(temp_str);

    return 0;
}

static void
    scheduler_thread_state_callback(FuriThread* thread, FuriThreadState state, void* context) {
    SchedulerApp* app = context;

    if(state == FuriThreadStateStopped) {
        FURI_LOG_I(TAG, "Thread stopped");
        // When the scene is exiting it joins and frees the thread itself
        furi_mutex_acquire(app->thread_mutex, FuriWaitForever);
        bool exiting = app->exiting;
        if(!exiting) {
            furi_thread_free(thread);
            app->thread = NULL;
        }
        furi_mutex_release(app->thread_mutex);
        app->is_transmitting = false;
        if(!exiting && scheduler_get_mode(app->scheduler) == SchedulerTxModeOneShot) {
            // this callback runs on the thread-scrub thread: hand the scene switch
            // to the GUI thread instead of switching (and freeing UI state) from here
            view_dispatcher_send_custom_event(app->view_dispatcher, SchedulerCustomEventRunDone);
        }
    } else if(state == FuriThreadStateStarting) {
        FURI_LOG_I(TAG, "Thread starting");
        app->is_transmitting = true;
    }
}

void scheduler_start_tx(SchedulerApp* app) {
    app->stop_tx = false;
    app->thread = furi_thread_alloc_ex("SchedulerTxThread", 4096, scheduler_tx, app);
    furi_thread_set_state_callback(app->thread, scheduler_thread_state_callback);
    furi_thread_set_state_context(app->thread, app);
    furi_thread_start(app->thread);
}
