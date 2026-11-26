#include "../camera_suite.h"
#include <furi.h>
#include <string.h>
#include <furi_hal.h>
#include <input/input.h>
#include <gui/elements.h>
#include <dolphin/dolphin.h>
#include "../helpers/camera_suite_haptic.h"
#include "../helpers/camera_suite_speaker.h"
#include "../helpers/camera_suite_led.h"

static uint8_t get_pixel(uint8_t* buf, uint8_t x, uint8_t y) {
    uint32_t pix_cnt = (y * FRAME_WIDTH) + x;
    uint32_t idx = pix_cnt / 8;
    uint8_t bit = pix_cnt % 8;
    return buf[idx] & (1 << (7 - bit)) ? 1 : 0;
}

static void draw_image(Canvas* canvas, uint8_t* cam_buf, uint8_t orientation) {
    furi_assert(canvas);
    furi_assert(orientation);

    for(size_t y = 0; y < FRAME_HEIGHT; y++) {
        for(size_t x = 0; x < FRAME_WIDTH; x++) {
            uint8_t x_cam;
            uint8_t y_cam;

            switch(orientation) {
            default:
            case 0: { // Camera rotated 0 degrees (right side up, default)
                x_cam = x;
                y_cam = y + 32;
                break;
            }
            case 1: { // Camera rotated 90 degrees
                x_cam = FRAME_WIDTH - y - 32 - 1;
                y_cam = x;
                break;
            }
            case 2: { // Camera rotated 180 degrees (upside down)
                x_cam = FRAME_WIDTH - x - 1;
                y_cam = FRAME_WIDTH - y - 32 - 1;
                break;
            }
            case 3: { // Camera rotated 270 degrees
                x_cam = y + 32;
                y_cam = FRAME_WIDTH - x - 1;
                break;
            }
            }

            uint8_t pixel = get_pixel(cam_buf, x_cam, y_cam);
            if(pixel) {
                canvas_draw_dot(canvas, x, FRAME_HEIGHT - y - 1);
            }
        }
    }
}

static void camera_suite_view_camera_draw(Canvas* canvas, void* model) {
    furi_assert(canvas);
    furi_assert(model);

    UartDumpModel* uartDumpModel = model;

    // Clear the screen.
    canvas_set_color(canvas, ColorBlack);

    // Draw the frame.
    canvas_draw_frame(canvas, 0, 0, FRAME_WIDTH, FRAME_HEIGHT);

    // Draw the image
    draw_image(canvas, uartDumpModel->pixels, uartDumpModel->orientation);

    // Draw the pinout guide if the camera is not initialized.
    if(!uartDumpModel->is_initialized) {
        // Clear the screen.
        canvas_clear(canvas);

        // Set the font to the secondary font.
        canvas_set_font(canvas, FontSecondary);

        // Draw the ESP32-CAM module.
        canvas_draw_str(canvas, 47, 50, "ESP32");
        canvas_draw_str(canvas, 52, 58, "CAM");
        canvas_draw_dot(canvas, 84, 3);
        canvas_draw_box(canvas, 50, 35, 23, 7);
        canvas_draw_circle(canvas, 42, 12, 1);
        canvas_draw_circle(canvas, 42, 16, 1);
        canvas_draw_circle(canvas, 42, 20, 1);
        canvas_draw_circle(canvas, 42, 24, 1);
        canvas_draw_circle(canvas, 42, 28, 1);
        canvas_draw_circle(canvas, 42, 32, 1);
        canvas_draw_circle(canvas, 42, 36, 1);
        canvas_draw_circle(canvas, 42, 8, 1);
        canvas_draw_circle(canvas, 59, 15, 1);
        canvas_draw_circle(canvas, 61, 17, 5);
        canvas_draw_circle(canvas, 61, 17, 9);
        canvas_draw_circle(canvas, 80, 12, 1);
        canvas_draw_circle(canvas, 80, 16, 1);
        canvas_draw_circle(canvas, 80, 20, 1);
        canvas_draw_circle(canvas, 80, 24, 1);
        canvas_draw_circle(canvas, 80, 28, 1);
        canvas_draw_circle(canvas, 80, 32, 1);
        canvas_draw_circle(canvas, 80, 36, 1);
        canvas_draw_circle(canvas, 80, 42, 1);
        canvas_draw_circle(canvas, 80, 8, 1);
        canvas_draw_line(canvas, 38, 4, 38, 58);
        canvas_draw_line(canvas, 39, 3, 83, 3);
        canvas_draw_line(canvas, 40, 2, 84, 2);
        canvas_draw_line(canvas, 48, 4, 74, 4);
        canvas_draw_line(canvas, 48, 5, 48, 26);
        canvas_draw_line(canvas, 55, 27, 49, 27);
        canvas_draw_line(canvas, 56, 25, 56, 36);
        canvas_draw_line(canvas, 64, 21, 63, 21);
        canvas_draw_line(canvas, 65, 15, 65, 17);
        canvas_draw_line(canvas, 66, 15, 64, 18);
        canvas_draw_line(canvas, 66, 16, 64, 19);
        canvas_draw_line(canvas, 66, 18, 60, 21);
        canvas_draw_line(canvas, 66, 19, 61, 21);
        canvas_draw_line(canvas, 66, 25, 66, 36);
        canvas_draw_line(canvas, 73, 27, 67, 27);
        canvas_draw_line(canvas, 74, 5, 74, 26);
        canvas_draw_line(canvas, 75, 4, 75, 25);
        canvas_draw_line(canvas, 83, 59, 39, 59);
        canvas_draw_line(canvas, 84, 4, 84, 58);
        canvas_draw_line(canvas, 85, 2, 85, 57);
        canvas_draw_frame(canvas, 78, 40, 5, 5);

        // Draw the pinout lines.
        canvas_draw_line(canvas, 39, 12, 21, 12);
        canvas_draw_line(canvas, 87, 24, 83, 24);
        canvas_draw_line(canvas, 87, 32, 83, 32);
        canvas_draw_line(canvas, 88, 23, 88, 13);
        canvas_draw_line(canvas, 88, 33, 88, 43);
        canvas_draw_line(canvas, 89, 12, 126, 12);
        canvas_draw_line(canvas, 126, 28, 83, 28);
        canvas_draw_line(canvas, 126, 44, 89, 44);

        // Draw the pinout labels.
        canvas_draw_str(canvas, 91, 11, "VCC-3V");
        canvas_draw_str(canvas, 91, 27, "U0R-TX");
        canvas_draw_str(canvas, 91, 43, "U0T-RX");
        canvas_draw_str(canvas, 2, 16, "GND");
        canvas_draw_str(canvas, 12, 25, "-GND");

        // Draw the "Please Connect Module!" text.
        canvas_draw_str(canvas, 2, 40, "Please");
        canvas_draw_str(canvas, 2, 49, "Connect");
        canvas_draw_str(canvas, 2, 58, "Module!");

        // Draw the "Back" text and button logo.
        canvas_draw_str(canvas, 92, 57, "Back");
        canvas_draw_line(canvas, 116, 49, 116, 53);
        canvas_draw_line(canvas, 115, 50, 115, 52);
        canvas_draw_dot(canvas, 114, 51);
        canvas_draw_line(canvas, 117, 51, 121, 51);
        canvas_draw_line(canvas, 122, 52, 123, 53);
        canvas_draw_line(canvas, 123, 54, 122, 55);
        canvas_draw_line(canvas, 121, 56, 117, 56);
    }
}

// Works on a private snapshot: SD I/O must never run under the view-model lock (GUI draw needs it).
static void save_image_to_flipper_sd_card(uint8_t* pixels, bool is_inverted) {
    furi_assert(pixels);

    // This pointer is used to access the storage.
    Storage* storage = furi_record_open(RECORD_STORAGE);

    // This pointer is used to access the filesystem.
    File* file = storage_file_alloc(storage);

    // Store path in local variable.
    const char* folderName = EXT_PATH("DCIM");

    // Create the folder name for the image file if it does not exist.
    if(storage_common_stat(storage, folderName, NULL) == FSE_NOT_EXIST) {
        storage_simply_mkdir(storage, folderName);
    }

    // This pointer is used to access the file name.
    FuriString* file_name = furi_string_alloc();

    // Get the current date and time.
    DateTime datetime = {0};

    furi_hal_rtc_get_datetime(&datetime);

    // Create the file name.
    furi_string_printf(
        file_name,
        EXT_PATH("DCIM/%.4d%.2d%.2d-%.2d%.2d%.2d.bmp"),
        datetime.year,
        datetime.month,
        datetime.day,
        datetime.hour,
        datetime.minute,
        datetime.second);

    // Open the file for writing. If the file does not exist (it shouldn't),
    // create it.
    bool result =
        storage_file_open(file, furi_string_get_cstr(file_name), FSAM_WRITE, FSOM_OPEN_ALWAYS);

    // Free the file name after use.
    furi_string_free(file_name);

    if(!is_inverted) {
        for(size_t i = 0; i < FRAME_BUFFER_LENGTH; ++i) {
            pixels[i] = ~pixels[i];
        }
    }

    // If the file was opened successfully, write the bitmap header and the
    // image data.
    if(result) {
        // Write BMP Header
        storage_file_write(file, bitmap_header, BITMAP_HEADER_LENGTH);

        // @todo - Add a function for saving the image directly from the
        // ESP32-CAM to the Flipper Zero SD card.

        // Write locally to the Flipper Zero SD card in the DCIM folder.
        int8_t row_buffer[ROW_BUFFER_LENGTH];

        // @todo - Save image based on orientation.
        for(size_t i = 64; i > 0; --i) {
            for(size_t j = 0; j < ROW_BUFFER_LENGTH; ++j) {
                row_buffer[j] = pixels[((i + 32 - 1) * ROW_BUFFER_LENGTH) + j];
            }
            storage_file_write(file, row_buffer, ROW_BUFFER_LENGTH);
        }
    }

    // Close the file.
    storage_file_close(file);

    // Free up memory.
    storage_file_free(file);
}

static void
    camera_suite_view_camera_model_init(UartDumpModel* const model, CameraSuite* instance_context) {
    furi_assert(model);
    furi_assert(instance_context);

    model->is_dithering_enabled = true;
    model->is_inverted = false;
    uint32_t orientation = instance_context->orientation;
    model->orientation = orientation;

    for(size_t i = 0; i < FRAME_BUFFER_LENGTH; i++) {
        model->pixels[i] = 0;
    }
}

static bool camera_suite_view_camera_input(InputEvent* event, void* context) {
    furi_assert(context);
    furi_assert(event);

    CameraSuiteViewCamera* instance = context;

    // Haptics, LED, UART delays and SD I/O all run OUTSIDE the view-model lock: the GUI draw
    // callback takes that lock with FuriWaitForever, so holding it here freezes the whole GUI.
    if(event->type == InputTypeRelease) {
        switch(event->key) {
        default: // Stop all sounds, reset the LED.
            camera_suite_play_bad_bump(instance->context);
            camera_suite_stop_all_sound(instance->context);
            camera_suite_led_set_rgb(instance->context, 0, 0, 0);
            break;
        }
    } else if(event->type == InputTypePress) {
        switch(event->key) {
        case InputKeyBack: {
            // Stop camera stream.
            furi_hal_serial_tx(instance->serial_handle, (uint8_t[]){'s'}, 1);
            furi_delay_ms(50);

            // Go back to the main menu.
            instance->callback(CameraSuiteCustomEventSceneCameraBack, instance->context);
            break;
        }
        case InputKeyLeft: {
            // Play sound.
            camera_suite_play_happy_bump(instance->context);
            camera_suite_play_input_sound(instance->context);
            camera_suite_led_set_rgb(instance->context, 0, 0, 255);

            bool is_inverted = false;
            with_view_model(
                instance->view, UartDumpModel * model, { is_inverted = model->is_inverted; }, false);

            if(is_inverted) {
                // Camera: Set invert to false on the ESP32-CAM.
                furi_hal_serial_tx(instance->serial_handle, (uint8_t[]){'i'}, 1);
            } else {
                // Camera: Set invert to true on the ESP32-CAM.
                furi_hal_serial_tx(instance->serial_handle, (uint8_t[]){'I'}, 1);
            }
            furi_delay_ms(50);

            with_view_model(
                instance->view, UartDumpModel * model, { model->is_inverted = !is_inverted; }, true);

            instance->callback(CameraSuiteCustomEventSceneCameraLeft, instance->context);
            break;
        }
        case InputKeyRight: {
            // Play sound.
            camera_suite_play_happy_bump(instance->context);
            camera_suite_play_input_sound(instance->context);
            camera_suite_led_set_rgb(instance->context, 0, 0, 255);

            bool is_dithering_enabled = false;
            with_view_model(
                instance->view,
                UartDumpModel * model,
                { is_dithering_enabled = model->is_dithering_enabled; },
                false);

            if(is_dithering_enabled) {
                // Camera: Disable dithering.
                furi_hal_serial_tx(instance->serial_handle, (uint8_t[]){'d'}, 1);
            } else {
                // Camera: Enable dithering.
                furi_hal_serial_tx(instance->serial_handle, (uint8_t[]){'D'}, 1);
            }
            furi_delay_ms(50);

            with_view_model(
                instance->view,
                UartDumpModel * model,
                { model->is_dithering_enabled = !is_dithering_enabled; },
                true);

            instance->callback(CameraSuiteCustomEventSceneCameraRight, instance->context);
            break;
        }
        case InputKeyUp: {
            // Play sound.
            camera_suite_play_happy_bump(instance->context);
            camera_suite_play_input_sound(instance->context);
            camera_suite_led_set_rgb(instance->context, 0, 0, 255);

            // Camera: Increase contrast.
            furi_hal_serial_tx(instance->serial_handle, (uint8_t[]){'C'}, 1);
            furi_delay_ms(50);

            instance->callback(CameraSuiteCustomEventSceneCameraUp, instance->context);
            break;
        }
        case InputKeyDown: {
            // Play sound.
            camera_suite_play_happy_bump(instance->context);
            camera_suite_play_input_sound(instance->context);
            camera_suite_led_set_rgb(instance->context, 0, 0, 255);

            // Camera: Reduce contrast.
            furi_hal_serial_tx(instance->serial_handle, (uint8_t[]){'c'}, 1);
            furi_delay_ms(50);

            instance->callback(CameraSuiteCustomEventSceneCameraDown, instance->context);
            break;
        }
        case InputKeyOk: {
            // Play sound.
            camera_suite_play_long_bump(instance->context);
            camera_suite_play_input_sound(instance->context);
            camera_suite_led_set_rgb(instance->context, 0, 0, 255);

            // @todo - Save picture directly to ESP32-CAM.
            // furi_hal_serial_tx(instance->serial_handle, (uint8_t[]){'P'}, 1);

            // Snapshot the displayed image under the lock, save it to the SD card after releasing it.
            uint8_t* snapshot = malloc(FRAME_BUFFER_LENGTH);
            if(snapshot) {
                bool is_inverted = false;
                with_view_model(
                    instance->view,
                    UartDumpModel * model,
                    {
                        memcpy(snapshot, model->pixels, FRAME_BUFFER_LENGTH);
                        is_inverted = model->is_inverted;
                    },
                    false);

                // Save currently displayed image to the Flipper Zero SD card.
                save_image_to_flipper_sd_card(snapshot, is_inverted);
                free(snapshot);
            }

            instance->callback(CameraSuiteCustomEventSceneCameraOk, instance->context);
            break;
        }
        case InputKeyMAX:
        default: {
            break;
        }
        }
    }

    return false;
}

static void camera_suite_view_camera_exit(void* context) {
    furi_assert(context);
    CameraSuiteViewCamera* instance = context;
    // Close while a key is held skips the Release handler: free speaker mutex + LED
    camera_suite_stop_all_sound(instance->context);
    camera_suite_led_set_rgb(instance->context, 0, 0, 0);
}

static void camera_suite_view_camera_enter(void* context) {
    furi_assert(context);

    // Get the camera suite instance context.
    CameraSuiteViewCamera* instance = (CameraSuiteViewCamera*)context;

    // Get the camera suite instance context.
    CameraSuite* instance_context = instance->context;

    // Start camera stream.
    furi_hal_serial_tx(instance->serial_handle, (uint8_t[]){'S'}, 1);
    furi_delay_ms(50);

    // Get/set dither type.
    uint8_t dither_type = instance_context->dither;
    furi_hal_serial_tx(instance->serial_handle, &dither_type, 1);
    furi_delay_ms(50);

    // Make sure the camera is not inverted.
    furi_hal_serial_tx(instance->serial_handle, (uint8_t[]){'i'}, 1);
    furi_delay_ms(50);

    // Toggle flash on or off based on the current state. If the user has this
    // on the flash will stay on the entire time the user is in the camera view.
    uint8_t flash_state = instance_context->flash ? 'F' : 'f';
    furi_hal_serial_tx(instance->serial_handle, &flash_state, 1);
    furi_delay_ms(50);

    with_view_model(
        instance->view,
        UartDumpModel * model,
        { camera_suite_view_camera_model_init(model, instance_context); },
        true);
}

static void
    camera_on_irq_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    furi_assert(handle);
    furi_assert(context);

    // Cast `context` to `CameraSuiteViewCamera*` and store it in `instance`.
    CameraSuiteViewCamera* instance = context;

    if(event == FuriHalSerialRxEventData) {
        uint8_t data = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(instance->camera_rx_stream, &data, 1, 0);
        furi_thread_flags_set(furi_thread_get_id(instance->camera_worker_thread), WorkerEventRx);
    }
}

// Returns true when a full row was completed (the picture changed).
static bool process_ringbuffer(UartDumpModel* model, uint8_t const byte) {
    furi_assert(model);
    furi_assert(byte);

    // The first HEADER_LENGTH bytes are reserved for header information.
    if(model->ringbuffer_index < HEADER_LENGTH) {
        // Validate the start of row characters 'Y' and ':'.
        if(model->ringbuffer_index == 0 && byte != 'Y') {
            // Incorrect start of frame; reset.
            return false;
        }
        if(model->ringbuffer_index == 1 && byte != ':') {
            // Incorrect start of frame; reset.
            model->ringbuffer_index = 0;
            return false;
        }
        if(model->ringbuffer_index == 2) {
            // Assign the third byte as the row identifier.
            model->row_identifier = byte;
        }
        model->ringbuffer_index++; // Increment index for the next byte.
        return false;
    }

    // Store pixel value directly after the header.
    model->row_ringbuffer[model->ringbuffer_index - HEADER_LENGTH] = byte;
    model->ringbuffer_index++; // Increment index for the next byte.

    // Check whether the ring buffer is filled.
    if(model->ringbuffer_index >= RING_BUFFER_LENGTH) {
        model->ringbuffer_index = 0; // Reset the ring buffer index.
        model->is_initialized = true; // Set the connection as successfully established.

        // Compute the starting index for the row in the pixel buffer.
        size_t row_start_index = model->row_identifier * ROW_BUFFER_LENGTH;

        // Ensure the row start index is within the valid range.
        if(row_start_index > LAST_ROW_INDEX) {
            return true; // Drop the row instead of overwriting row 0.
        }

        // Flush the contents of the ring buffer to the pixel buffer.
        for(size_t i = 0; i < ROW_BUFFER_LENGTH; ++i) {
            model->pixels[row_start_index + i] = model->row_ringbuffer[i];
        }
        return true;
    }
    return false;
}

#define CAMERA_REDRAW_MS 70

static int32_t camera_suite_camera_worker(void* context) {
    furi_assert(context);

    CameraSuiteViewCamera* instance = context;

    // Redraw at most every CAMERA_REDRAW_MS: a redraw per UART burst keeps the GUI thread at 100%.
    bool dirty = false;
    uint32_t last_redraw = 0;

    while(1) {
        // Wait for any event on the worker thread; while a redraw is pending only until it is due.
        uint32_t wait_ms = FuriWaitForever;
        if(dirty) {
            uint32_t elapsed = furi_get_tick() - last_redraw;
            wait_ms = (elapsed >= CAMERA_REDRAW_MS) ? 0 : (CAMERA_REDRAW_MS - elapsed);
        }
        uint32_t events =
            furi_thread_flags_wait(CAMERA_WORKER_EVENTS_MASK, FuriFlagWaitAny, wait_ms);

        if(events == (uint32_t)FuriFlagErrorTimeout) {
            // Only the pending redraw is due.
            events = 0;
        } else {
            // Check if an error occurred.
            furi_check((events & FuriFlagError) == 0);
        }

        // Check if the thread should stop.
        if(events & WorkerEventStop) {
            break;
        } else if(events & WorkerEventRx) {
            size_t length = 0;
            // Read all available data from the stream buffer.
            do {
                // Read up to 64 bytes from the stream buffer.
                size_t buffer_size = 64;
                // Allocate a buffer for the data.
                uint8_t data[buffer_size];
                // Read the data from the stream buffer.
                length =
                    furi_stream_buffer_receive(instance->camera_rx_stream, data, buffer_size, 0);
                if(length > 0) {
                    with_view_model(
                        instance->view,
                        UartDumpModel * model,
                        {
                            // Process the data.
                            for(size_t i = 0; i < length; i++) {
                                if(process_ringbuffer(model, data[i])) dirty = true;
                            }
                        },
                        false);
                }
            } while(length > 0);
        }

        if(dirty && (furi_get_tick() - last_redraw) >= CAMERA_REDRAW_MS) {
            with_view_model(instance->view, UartDumpModel * model, { UNUSED(model); }, true);
            dirty = false;
            last_redraw = furi_get_tick();
        }
    }

    return 0;
}

CameraSuiteViewCamera* camera_suite_view_camera_alloc() {
    // Allocate memory for the instance
    CameraSuiteViewCamera* instance = malloc(sizeof(CameraSuiteViewCamera));

    // Allocate the view object
    instance->view = view_alloc();

    // Allocate a stream buffer
    instance->camera_rx_stream = furi_stream_buffer_alloc(2048, 1);

    // Allocate model
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(UartDumpModel));

    // Set context for the view
    view_set_context(instance->view, instance);

    // Set draw callback
    view_set_draw_callback(instance->view, (ViewDrawCallback)camera_suite_view_camera_draw);

    // Set input callback
    view_set_input_callback(instance->view, camera_suite_view_camera_input);

    // Set enter callback
    view_set_enter_callback(instance->view, camera_suite_view_camera_enter);

    // Set exit callback
    view_set_exit_callback(instance->view, camera_suite_view_camera_exit);

    // Allocate a thread for this camera to run on.
    FuriThread* thread = furi_thread_alloc_ex(
        "Camera_Suite_Camera_Rx_Thread", 2048, camera_suite_camera_worker, instance);
    instance->camera_worker_thread = thread;
    furi_thread_start(instance->camera_worker_thread);

    // Allocate the serial handle for the camera.
    instance->serial_handle = furi_hal_serial_control_acquire(UART_CH);
    furi_check(instance->serial_handle);
    furi_hal_serial_init(instance->serial_handle, 230400);

    // Start the asynchronous receive.
    furi_hal_serial_async_rx_start(instance->serial_handle, camera_on_irq_cb, instance, false);

    return instance;
}

void camera_suite_view_camera_free(CameraSuiteViewCamera* instance) {
    furi_assert(instance);

    // Deinitialize the serial handle and release the control.
    furi_hal_serial_async_rx_stop(instance->serial_handle);
    furi_hal_serial_deinit(instance->serial_handle);
    furi_hal_serial_control_release(instance->serial_handle);

    // Free the worker thread.
    furi_thread_flags_set(furi_thread_get_id(instance->camera_worker_thread), WorkerEventStop);
    furi_thread_join(instance->camera_worker_thread);
    furi_thread_free(instance->camera_worker_thread);

    // Free the allocated stream buffer.
    furi_stream_buffer_free(instance->camera_rx_stream);

    with_view_model(instance->view, UartDumpModel * model, { UNUSED(model); }, true);
    view_free(instance->view);
    free(instance);
}

View* camera_suite_view_camera_get_view(CameraSuiteViewCamera* instance) {
    furi_assert(instance);
    return instance->view;
}

void camera_suite_view_camera_set_callback(
    CameraSuiteViewCamera* instance,
    CameraSuiteViewCameraCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(callback);
    instance->callback = callback;
    instance->context = context;
}
