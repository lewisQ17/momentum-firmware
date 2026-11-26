#include <stdio.h>
#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <dialogs/dialogs.h>
#include <input/input.h>
#include <notification/notification_messages.h>

#include <storage/storage.h>
#include <stream/stream.h>
#include <stream/buffered_file_stream.h>
#include <toolbox/stream/file_stream.h>

#include <hex_editor_icons.h>
#include <assets_icons.h>

#define TAG "HexEditor"

#define STEP 6u

typedef struct {
    uint32_t file_offset;
    uint32_t file_read_bytes;
    uint32_t file_size;
    uint8_t string_offset;
    char editable_char;
    Stream* stream;
    bool mode;
} HexEditorModel;

typedef struct {
    HexEditorModel* model;

    FuriMessageQueue* input_queue;

    ViewPort* view_port;
    Gui* gui;
    Storage* storage;

    FuriString* buffer;
    FuriMutex* mutex;
} HexEditor;

// Max data bytes kept per line. string_offset is a uint8_t, so a line (plus its
// terminator) must stay <= 256 bytes anyway; this also bounds heap use on
// newline-free files, where stream_read_line would copy the whole file.
#define HEX_LINE_MAX_DATA 255

// Like stream_read_line, but drops everything past HEX_LINE_MAX_DATA bytes of a
// line. The stream is still left just after the line's '\n', which the Up/edit
// logic relies on. A truncated line ends in a synthetic '\n' (never editable).
static bool hex_read_line(Stream* stream, FuriString* str_result) {
    // build into a temp so a failed read (EOF) keeps the current line in str_result
    FuriString* line = furi_string_alloc();
    uint8_t buffer[32];
    bool truncated = false;

    do {
        uint16_t bytes_were_read = stream_read(stream, buffer, sizeof(buffer));
        if(bytes_were_read == 0) break;

        bool result = false;
        bool error = false;
        for(uint16_t i = 0; i < bytes_were_read; i++) {
            if(buffer[i] == '\n') {
                if(!stream_seek(stream, i - bytes_were_read + 1, StreamOffsetFromCurrent)) {
                    error = true;
                    break;
                }
                furi_string_push_back(line, buffer[i]);
                result = true;
                break;
            } else if(buffer[i] == '\r') {
                // Ignore
            } else if(furi_string_size(line) < HEX_LINE_MAX_DATA) {
                furi_string_push_back(line, buffer[i]);
            } else {
                truncated = true;
            }
        }

        if(result || error) {
            // a real newline already terminates the string
            truncated = false;
            break;
        }
    } while(true);

    if(truncated) furi_string_push_back(line, '\n');

    const bool has_line = furi_string_size(line) != 0;
    if(has_line) furi_string_swap(str_result, line);
    furi_string_free(line);

    return has_line;
}

static void draw_callback(Canvas* canvas, void* ctx) {
    HexEditor* hex_editor = ctx;

    furi_mutex_acquire(hex_editor->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "Line and mode:");

    canvas_set_font(canvas, FontSecondary);

    uint8_t com_str_offset = 0;
    uint8_t local_offset = MAX(hex_editor->model->string_offset - 5, 0);
    // TODO UTF ?
    for(uint8_t i = 0; i < MIN((uint8_t)128 / STEP, furi_string_size(hex_editor->buffer)); i++) {
        if(i + local_offset >= furi_string_size(hex_editor->buffer)) {
            break;
        }
        char a = furi_string_get_char(hex_editor->buffer, i + local_offset);
        canvas_draw_glyph(
            canvas, 0 + com_str_offset + (STEP - canvas_glyph_width(canvas, a)) / 2, 20, a);

        com_str_offset += STEP;
    }

    canvas_draw_icon(
        canvas,
        0 + MIN(hex_editor->model->string_offset, 5) * STEP - 4 + STEP / 2,
        21,
        &I_Pin_arrow_up_7x9);

    if(hex_editor->model->mode) {
        elements_button_left(canvas, "ASCII -");
        elements_button_right(canvas, "ASCII +");
        elements_button_center(canvas, "");

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 45, "edit");
    } else {
        elements_button_left(canvas, "");
        elements_button_right(canvas, "");
        elements_button_center(canvas, "chmod");

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 45, "seek");
    }

    canvas_draw_glyph(canvas, 30, 45, hex_editor->model->editable_char);

    furi_mutex_release(hex_editor->mutex);
}

static void input_callback(InputEvent* input_event, void* ctx) {
    // Проверяем, что контекст не нулевой
    furi_assert(ctx);
    HexEditor* hex_editor = ctx;

    furi_message_queue_put(hex_editor->input_queue, input_event, 100);
}

static HexEditor* hex_editor_alloc() {
    HexEditor* instance = malloc(sizeof(HexEditor));

    instance->model = malloc(sizeof(HexEditorModel));
    memset(instance->model, 0x0, sizeof(HexEditorModel));

    instance->model->editable_char = ' ';

    instance->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    instance->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    instance->view_port = view_port_alloc();
    view_port_draw_callback_set(instance->view_port, draw_callback, instance);
    view_port_input_callback_set(instance->view_port, input_callback, instance);

    instance->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(instance->gui, instance->view_port, GuiLayerFullscreen);

    instance->storage = furi_record_open(RECORD_STORAGE);

    instance->buffer = furi_string_alloc();

    return instance;
}

static void hex_editor_free(HexEditor* instance) {
    furi_record_close(RECORD_STORAGE);

    gui_remove_view_port(instance->gui, instance->view_port);
    view_port_free(instance->view_port);
    furi_record_close(RECORD_GUI);

    furi_message_queue_free(instance->input_queue);

    if(instance->model->stream) {
        buffered_file_stream_close(instance->model->stream);
        stream_free(instance->model->stream);
    }

    furi_string_free(instance->buffer);
    furi_mutex_free(instance->mutex);

    free(instance->model);
    free(instance);
}

static bool hex_editor_open_file(HexEditor* hex_editor, const char* file_path) {
    furi_assert(hex_editor);
    furi_assert(file_path);

    hex_editor->model->stream = buffered_file_stream_alloc(hex_editor->storage);

    if(!buffered_file_stream_open(
           hex_editor->model->stream, file_path, FSAM_READ_WRITE, FSOM_OPEN_EXISTING)) {
        FURI_LOG_E(TAG, "Unable to open stream: %s", file_path);
        return false;
    };

    hex_editor->model->file_size = stream_size(hex_editor->model->stream);

    return true;
}

int32_t hex_editor_app(void* p) {
    UNUSED(p);

    HexEditor* hex_editor = hex_editor_alloc();

    FuriString* file_path;
    file_path = furi_string_alloc();

    do {
        if(p && strlen(p)) {
            furi_string_set(file_path, (const char*)p);
        } else {
            furi_string_set(file_path, STORAGE_EXT_PATH_PREFIX);

            DialogsFileBrowserOptions browser_options;
            dialog_file_browser_set_basic_options(&browser_options, "*", &I_edit_10px);
            browser_options.hide_ext = false;
            browser_options.hide_dot_files = false;

            DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
            bool res = dialog_file_browser_show(dialogs, file_path, file_path, &browser_options);

            furi_record_close(RECORD_DIALOGS);
            if(!res) {
                FURI_LOG_I(TAG, "No file selected");
                break;
            }
        }

        FURI_LOG_I(TAG, "File selected: %s", furi_string_get_cstr(file_path));

        if(!hex_editor_open_file(hex_editor, furi_string_get_cstr(file_path))) break;

        furi_mutex_acquire(hex_editor->mutex, FuriWaitForever);
        bool has_line = hex_read_line(hex_editor->model->stream, hex_editor->buffer);
        furi_mutex_release(hex_editor->mutex);
        if(!has_line) {
            FURI_LOG_T(TAG, "No keys left in dict");
            break;
        }

        InputEvent event;
        int8_t offset_modifier;
        while(1) {
            // Выбираем событие из очереди в переменную event (ждем бесконечно долго, если очередь пуста)
            // и проверяем, что у нас получилось это сделать
            furi_check(
                furi_message_queue_get(hex_editor->input_queue, &event, FuriWaitForever) ==
                FuriStatusOk);

            // Если нажата кнопка "назад", то выходим из цикла, а следовательно и из приложения
            furi_mutex_acquire(hex_editor->mutex, FuriWaitForever);
            if(event.type == InputTypeShort || event.type == InputTypeRepeat) {
                if(!hex_editor->model->mode) {
                    offset_modifier = 1;
                    if(event.type == InputTypeRepeat) {
                        offset_modifier = 3;
                    }
                    // wrap in int math so repeat steps on 1-2 char lines stay inside the line
                    const int line_size = (int)furi_string_size(hex_editor->buffer);
                    if(event.key == InputKeyRight && line_size > 0) {
                        hex_editor->model->string_offset =
                            (uint8_t)(((int)hex_editor->model->string_offset + offset_modifier) %
                                      line_size);
                    }
                    if(event.key == InputKeyLeft && line_size > 0) {
                        hex_editor->model->string_offset = (uint8_t)((
                            (((int)hex_editor->model->string_offset - offset_modifier) % line_size) +
                            line_size) %
                                                                     line_size);
                    }
                    if(event.key == InputKeyDown) {
                        hex_editor->model->string_offset = 0;
                        if(!hex_read_line(hex_editor->model->stream, hex_editor->buffer)) {
                            FURI_LOG_T(TAG, "No keys left in dict");
                        }
                    }
                    if(event.key == InputKeyUp) {
                        hex_editor->model->string_offset = 0;

                        if(!stream_seek(hex_editor->model->stream, -1, StreamOffsetFromCurrent)) {
                            FURI_LOG_E(TAG, "Unable to seek stream");
                            furi_mutex_release(hex_editor->mutex);
                            break;
                        }
                        // NOT work on first line
                        stream_seek_to_char(
                            hex_editor->model->stream, '\n', StreamDirectionBackward);

                        if(!stream_seek_to_char(
                               hex_editor->model->stream, '\n', StreamDirectionBackward)) {
                            stream_rewind(hex_editor->model->stream);
                        } else {
                            if(!stream_seek(
                                   hex_editor->model->stream, 1, StreamOffsetFromCurrent)) {
                                FURI_LOG_E(TAG, "Unable to seek stream");
                                furi_mutex_release(hex_editor->mutex);
                                break;
                            }
                        }

                        if(!hex_read_line(hex_editor->model->stream, hex_editor->buffer)) {
                            FURI_LOG_T(TAG, "No keys left in dict");
                            furi_mutex_release(hex_editor->mutex);
                            break;
                        }
                    }

                    if(event.key == InputKeyOk &&
                       hex_editor->model->string_offset < furi_string_size(hex_editor->buffer)) {
                        char current_char = furi_string_get_char(
                            hex_editor->buffer, hex_editor->model->string_offset);

                        // Never edit the line terminator, it would merge lines
                        if(current_char != '\n') {
                            hex_editor->model->editable_char = current_char;

                            hex_editor->model->mode = 1;
                        }
                    }
                } else {
                    offset_modifier = 1;
                    if(event.type == InputTypeRepeat) {
                        offset_modifier = 4;
                    }
                    if(event.key == InputKeyRight) {
                        hex_editor->model->editable_char += offset_modifier;
                    }
                    if(event.key == InputKeyLeft) {
                        hex_editor->model->editable_char -= offset_modifier;
                    }

                    if(event.key == InputKeyOk) {
                        if(!stream_seek(hex_editor->model->stream, -1, StreamOffsetFromCurrent)) {
                            FURI_LOG_E(TAG, "Unable to seek stream");
                            furi_mutex_release(hex_editor->mutex);
                            break;
                        }
                        if(!stream_seek_to_char(
                               hex_editor->model->stream, '\n', StreamDirectionBackward)) {
                            FURI_LOG_E(TAG, "Unable to stream_seek_to_char n");
                            // first line
                            stream_seek(hex_editor->model->stream, 0, StreamOffsetFromStart);
                            if(!stream_seek(
                                   hex_editor->model->stream,
                                   hex_editor->model->string_offset,
                                   StreamOffsetFromCurrent)) {
                                FURI_LOG_E(TAG, "Unable to seek string_offset");
                            }
                        } else {
                            if(!stream_seek(
                                   hex_editor->model->stream,
                                   hex_editor->model->string_offset + 1,
                                   StreamOffsetFromCurrent)) {
                                FURI_LOG_E(TAG, "Unable to seek string_offset +1");
                            }
                        }

                        if(!stream_write_char(
                               hex_editor->model->stream, hex_editor->model->editable_char)) {
                            FURI_LOG_E(TAG, "Unable to stream_write_char");
                            furi_mutex_release(hex_editor->mutex);
                            break;
                        }

                        hex_editor->model->editable_char = ' ';

                        hex_editor->model->mode = 0;

                        if(!stream_seek_to_char(
                               hex_editor->model->stream, '\n', StreamDirectionBackward)) {
                            FURI_LOG_E(TAG, "Unable to stream_seek_to_char n 2");
                            // first line
                            stream_seek(hex_editor->model->stream, 0, StreamOffsetFromStart);
                        } else {
                            if(!stream_seek(
                                   hex_editor->model->stream, 1, StreamOffsetFromCurrent)) {
                                FURI_LOG_E(TAG, "Unable to seek stream");
                                furi_mutex_release(hex_editor->mutex);
                                break;
                            }
                        }

                        if(!hex_read_line(hex_editor->model->stream, hex_editor->buffer)) {
                            FURI_LOG_T(TAG, "No keys left in dict");
                            furi_mutex_release(hex_editor->mutex);
                            break;
                        }
                    }
                }
            }
            furi_mutex_release(hex_editor->mutex);
            if(event.key == InputKeyBack) {
                break;
            }
            // ?
            view_port_update(hex_editor->view_port);
        }
    } while(false);

    furi_string_free(file_path);
    hex_editor_free(hex_editor);

    return 0;
}
