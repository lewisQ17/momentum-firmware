#include "../../js_modules.h" // IWYU pragma: keep
#include "js_gui.h"
#include "../js_event_loop/js_event_loop.h"
#include <gui/modules/button_panel.h>
#include <toolbox/str_buffer.h>

// button_panel_reserve() crashes on 0 and the cell grid needs a sane upper bound
#define JS_BTN_PANEL_MAX_MATRIX 16

typedef struct {
    size_t matrix_x, matrix_y;
    // occupied cells, fixed stride so it stays valid when matrixSizeX/Y change
    uint8_t occupied[JS_BTN_PANEL_MAX_MATRIX * JS_BTN_PANEL_MAX_MATRIX / 8];
    int32_t next_index;
    bool has_children; // reserve() re-inits the label list, so it must not run over added children
    StrBuffer str_buffer;

    FuriMessageQueue* input_queue;
    JsEventLoopContract contract;
} JsBtnPanelContext;

typedef struct {
    int32_t index;
    InputType input_type;
} JsBtnPanelEvent;

static const char* js_input_type_to_str(InputType type) {
    switch(type) {
    case InputTypePress:
        return "press";
    case InputTypeRelease:
        return "release";
    case InputTypeShort:
        return "short";
    case InputTypeLong:
        return "long";
    case InputTypeRepeat:
        return "repeat";
    default:
        furi_crash();
    }
}

static mjs_val_t
    input_transformer(struct mjs* mjs, FuriMessageQueue* queue, JsBtnPanelContext* context) {
    UNUSED(context);
    JsBtnPanelEvent event;
    furi_check(furi_message_queue_get(queue, &event, 0) == FuriStatusOk);

    mjs_val_t event_obj = mjs_mk_object(mjs);
    JS_ASSIGN_MULTI(mjs, event_obj) {
        JS_FIELD("index", mjs_mk_number(mjs, event.index));
        JS_FIELD("type", mjs_mk_string(mjs, js_input_type_to_str(event.input_type), ~0, false));
    }

    return event_obj;
}

static void input_callback(void* ctx, int32_t index, InputType type) {
    JsBtnPanelContext* context = ctx;
    JsBtnPanelEvent event = {
        .index = index,
        .input_type = type,
    };
    (void)furi_message_queue_put(context->input_queue, &event, 0); // drop on full instead of rebooting the device
}

static void js_button_panel_reset_children(ButtonPanel* panel, JsBtnPanelContext* context);

static void js_button_panel_resize(ButtonPanel* panel, JsBtnPanelContext* context) {
    if(context->has_children) {
        // Drops existing children (script must re-add them) instead of leaking their label nodes
        js_button_panel_reset_children(panel, context);
    } else {
        button_panel_reserve(panel, context->matrix_x, context->matrix_y);
    }
}

static bool matrix_size_x_assign(
    struct mjs* mjs,
    ButtonPanel* panel,
    JsViewPropValue value,
    JsBtnPanelContext* context) {
    if(value.number < 1 || value.number > JS_BTN_PANEL_MAX_MATRIX) {
        mjs_prepend_errorf(mjs, MJS_BAD_ARGS_ERROR, "matrixSizeX must be 1..16");
        return false;
    }
    context->matrix_x = value.number;
    js_button_panel_resize(panel, context);
    return true;
}

static bool matrix_size_y_assign(
    struct mjs* mjs,
    ButtonPanel* panel,
    JsViewPropValue value,
    JsBtnPanelContext* context) {
    if(value.number < 1 || value.number > JS_BTN_PANEL_MAX_MATRIX) {
        mjs_prepend_errorf(mjs, MJS_BAD_ARGS_ERROR, "matrixSizeY must be 1..16");
        return false;
    }
    context->matrix_y = value.number;
    js_button_panel_resize(panel, context);
    return true;
}

static bool js_button_panel_add_child(
    struct mjs* mjs,
    ButtonPanel* panel,
    JsBtnPanelContext* context,
    mjs_val_t child_obj) {
    typedef enum {
        JsButtonPanelChildTypeButton,
        JsButtonPanelChildTypeLabel,
        JsButtonPanelChildTypeIcon,
    } JsButtonPanelChildType;
    static const JsValueEnumVariant js_button_panel_child_type_variants[] = {
        {"button", JsButtonPanelChildTypeButton},
        {"label", JsButtonPanelChildTypeLabel},
        {"icon", JsButtonPanelChildTypeIcon},
    };
    static const JsValueDeclaration js_button_panel_child_type =
        JS_VALUE_ENUM(JsButtonPanelChildType, js_button_panel_child_type_variants);

    static const JsValueDeclaration js_button_panel_number = JS_VALUE_SIMPLE(JsValueTypeInt32);
    static const JsValueObjectField js_button_panel_common_fields[] = {
        {"type", &js_button_panel_child_type},
        {"x", &js_button_panel_number},
        {"y", &js_button_panel_number},
    };
    static const JsValueDeclaration js_button_panel_common =
        JS_VALUE_OBJECT(js_button_panel_common_fields);

    static const JsValueDeclaration js_button_panel_pointer =
        JS_VALUE_SIMPLE(JsValueTypeRawPointer);
    static const JsValueObjectField js_button_panel_button_fields[] = {
        {"matrixX", &js_button_panel_number},
        {"matrixY", &js_button_panel_number},
        {"icon", &js_button_panel_pointer},
        {"iconSelected", &js_button_panel_pointer},
    };
    static const JsValueDeclaration js_button_panel_button =
        JS_VALUE_OBJECT(js_button_panel_button_fields);

    static const JsValueDeclaration js_button_panel_string = JS_VALUE_SIMPLE(JsValueTypeString);
    static const JsValueObjectField js_button_panel_label_fields[] = {
        {"text", &js_button_panel_string},
        {"font", &js_gui_font_declaration},
    };
    static const JsValueDeclaration js_button_panel_label =
        JS_VALUE_OBJECT(js_button_panel_label_fields);

    static const JsValueObjectField js_button_panel_icon_fields[] = {
        {"icon", &js_button_panel_pointer},
    };
    static const JsValueDeclaration js_button_panel_icon =
        JS_VALUE_OBJECT(js_button_panel_icon_fields);

    JsButtonPanelChildType child_type;
    int32_t x, y;
    JsValueParseStatus status;
    JS_VALUE_PARSE(
        mjs,
        JS_VALUE_PARSE_SOURCE_VALUE(&js_button_panel_common),
        JsValueParseFlagReturnOnError,
        &status,
        &child_obj,
        &child_type,
        &x,
        &y);
    if(status != JsValueParseStatusOk) return false;

    switch(child_type) {
    case JsButtonPanelChildTypeButton: {
        int32_t matrix_x, matrix_y;
        const Icon *icon, *icon_selected;
        JS_VALUE_PARSE(
            mjs,
            JS_VALUE_PARSE_SOURCE_VALUE(&js_button_panel_button),
            JsValueParseFlagReturnOnError,
            &status,
            &child_obj,
            &matrix_x,
            &matrix_y,
            &icon,
            &icon_selected);
        if(status != JsValueParseStatusOk) return false;
        // out-of-range or already used cells would trip furi_check in button_panel
        if(matrix_x < 0 || (size_t)matrix_x >= context->matrix_x || matrix_y < 0 ||
           (size_t)matrix_y >= context->matrix_y) {
            mjs_prepend_errorf(mjs, MJS_BAD_ARGS_ERROR, "button matrixX/matrixY out of range");
            return false;
        }
        size_t cell = (size_t)matrix_y * JS_BTN_PANEL_MAX_MATRIX + (size_t)matrix_x;
        if(context->occupied[cell / 8] & (1 << (cell % 8))) {
            mjs_prepend_errorf(mjs, MJS_BAD_ARGS_ERROR, "button matrix cell already in use");
            return false;
        }
        context->occupied[cell / 8] |= 1 << (cell % 8);
        context->has_children = true;
        button_panel_add_item(
            panel,
            context->next_index++,
            matrix_x,
            matrix_y,
            x,
            y,
            icon,
            icon_selected,
            (ButtonItemCallback)input_callback,
            context);
        break;
    }

    case JsButtonPanelChildTypeLabel: {
        const char* text;
        Font font;
        JS_VALUE_PARSE(
            mjs,
            JS_VALUE_PARSE_SOURCE_VALUE(&js_button_panel_label),
            JsValueParseFlagReturnOnError,
            &status,
            &child_obj,
            &text,
            &font);
        if(status != JsValueParseStatusOk) return false;
        button_panel_add_label(
            panel, x, y, font, str_buffer_make_owned_clone(&context->str_buffer, text));
        context->has_children = true;
        break;
    }

    case JsButtonPanelChildTypeIcon: {
        const Icon* icon;
        JS_VALUE_PARSE(
            mjs,
            JS_VALUE_PARSE_SOURCE_VALUE(&js_button_panel_icon),
            JsValueParseFlagReturnOnError,
            &status,
            &child_obj,
            &icon);
        if(status != JsValueParseStatusOk) return false;
        button_panel_add_icon(panel, x, y, icon);
        context->has_children = true;
        break;
    }
    }

    return true;
}

static void js_button_panel_reset_children(ButtonPanel* panel, JsBtnPanelContext* context) {
    context->next_index = 0;
    context->has_children = false;
    memset(context->occupied, 0, sizeof(context->occupied));
    button_panel_reset(panel);
    button_panel_reserve(panel, context->matrix_x, context->matrix_y);
    str_buffer_clear_all_clones(&context->str_buffer);
}

static JsBtnPanelContext* ctx_make(struct mjs* mjs, ButtonPanel* panel, mjs_val_t view_obj) {
    UNUSED(panel);
    JsBtnPanelContext* context = malloc(sizeof(JsBtnPanelContext));
    *context = (JsBtnPanelContext){
        .matrix_x = 1,
        .matrix_y = 1,
        .next_index = 0,
        .has_children = false,
        .str_buffer = {0},
        .input_queue = furi_message_queue_alloc(1, sizeof(JsBtnPanelEvent)),
    };
    context->contract = (JsEventLoopContract){
        .magic = JsForeignMagic_JsEventLoopContract,
        .object_type = JsEventLoopObjectTypeQueue,
        .object = context->input_queue,
        .non_timer =
            {
                .event = FuriEventLoopEventIn,
                .transformer = (JsEventLoopTransformer)input_transformer,
                .transformer_context = context,
            },
    };
    mjs_set(mjs, view_obj, "input", ~0, mjs_mk_foreign(mjs, &context->contract));
    return context;
}

static void ctx_destroy(ButtonPanel* input, JsBtnPanelContext* context, FuriEventLoop* loop) {
    UNUSED(input);
    furi_event_loop_maybe_unsubscribe(loop, context->input_queue);
    furi_message_queue_free(context->input_queue);
    str_buffer_clear_all_clones(&context->str_buffer);
    free(context);
}

static const JsViewDescriptor view_descriptor = {
    .alloc = (JsViewAlloc)button_panel_alloc,
    .free = (JsViewFree)button_panel_free,
    .get_view = (JsViewGetView)button_panel_get_view,
    .custom_make = (JsViewCustomMake)ctx_make,
    .custom_destroy = (JsViewCustomDestroy)ctx_destroy,
    .add_child = (JsViewAddChild)js_button_panel_add_child,
    .reset_children = (JsViewResetChildren)js_button_panel_reset_children,
    .prop_cnt = 2,
    .props = {
        (JsViewPropDescriptor){
            .name = "matrixSizeX",
            .type = JsViewPropTypeNumber,
            .assign = (JsViewPropAssign)matrix_size_x_assign},
        (JsViewPropDescriptor){
            .name = "matrixSizeY",
            .type = JsViewPropTypeNumber,
            .assign = (JsViewPropAssign)matrix_size_y_assign},
    }};

JS_GUI_VIEW_DEF(button_panel, &view_descriptor);
