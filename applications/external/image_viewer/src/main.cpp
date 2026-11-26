#include <furi.h>
#include <dialogs/dialogs.h>
#include <stream/file_stream.h>
#include <gui/gui.h>

#include <image_viewer_icons.h>

constexpr auto tag = "Image viewer";

constexpr uint32_t msg_count = 8UL;

Stream* stream;
static bool image_opened = false; // false when the browser was cancelled

// 1 header byte + 128x64 bitmap bits; read and bit-reversed once, not on every redraw
constexpr size_t image_size = ((128 * 64) / 8) + 1;
static uint8_t* image_data = nullptr;

uint8_t reverse_bits(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

static void draw_callback(Canvas* canvas, void* ctx) {
    UNUSED(ctx);

    const size_t width = canvas_width(canvas);
    const size_t height = canvas_height(canvas);

    canvas_clear(canvas);
    if(!image_opened || image_data == nullptr) return;

    // skip the 1-byte .bm header, otherwise every row is shifted by 8 px
    canvas_draw_bitmap(canvas, 0, 0, width, height, image_data + 1);
}

static void input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);

    FuriMessageQueue* event_queue = static_cast<FuriMessageQueue*>(ctx);
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

extern "C" {
int32_t image_viewer_main(void* p) {
    UNUSED(p);

    FuriMessageQueue* event_queue = furi_message_queue_alloc(msg_count, sizeof(InputEvent));

    Storage* storage = static_cast<Storage*>(furi_record_open(RECORD_STORAGE));

    stream = file_stream_alloc(storage);

    furi_record_close(RECORD_STORAGE);

    DialogsApp* dialogs = static_cast<DialogsApp*>(furi_record_open(RECORD_DIALOGS));

    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, ".bm", &I_icon);
    browser_options.hide_ext = false;

    FuriString* path = furi_string_alloc_set_str("/ext");

    bool selected = dialog_file_browser_show(dialogs, path, path, &browser_options);

    furi_record_close(RECORD_DIALOGS);

    if(selected) {
        if(!file_stream_open(stream, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING)) {
            FURI_LOG_E(tag, "Cannot open file \"%s\"", furi_string_get_cstr(path));
            furi_string_free(path);
            furi_message_queue_free(event_queue);
            stream_free(stream);
            return -1;
        }

        image_data = new uint8_t[image_size](); // zero-initialised

        // never read more than the buffer holds, whatever the file size is
        stream_read(stream, image_data, MIN(stream_size(stream), image_size));

        if(image_data[0] != 0) {
            // header byte 0 = uncompressed; anything else would be drawn as noise
            FURI_LOG_E(tag, "Unsupported (compressed) .bm file");
            delete[] image_data;
            image_data = nullptr;
        } else {
            for(size_t i = 1; i < image_size; ++i) {
                image_data[i] = reverse_bits(image_data[i]);
            }
            image_opened = true;
        }
    }

    furi_string_free(path);

    ViewPort* view_port = view_port_alloc();

    view_port_draw_callback_set(view_port, draw_callback, nullptr);

    view_port_input_callback_set(view_port, input_callback, event_queue);

    Gui* gui = static_cast<Gui*>(furi_record_open(RECORD_GUI));

    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;

    while(true) {
        if(furi_message_queue_get(event_queue, &event, FuriWaitForever) == FuriStatusOk) {
            if(event.key == InputKeyBack) {
                break;
            }
            view_port_update(view_port);
        }
    }

    view_port_enabled_set(view_port, false);

    gui_remove_view_port(gui, view_port);

    view_port_free(view_port);

    furi_message_queue_free(event_queue);

    furi_record_close(RECORD_GUI);

    stream_free(stream);

    delete[] image_data;
    image_data = nullptr;

    return 0;
}
}
