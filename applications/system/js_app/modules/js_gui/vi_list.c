#include "../../js_modules.h" // IWYU pragma: keep
#include "js_gui.h"
#include "../js_event_loop/js_event_loop.h"
#include <gui/modules/variable_item_list.h>
#include <toolbox/str_buffer.h>

typedef struct JsViListContext JsViListContext;

// Per-item data owned by the list context; variants are C copies so the GUI thread never uses mjs
typedef struct {
    JsViListContext* context;
    uint8_t item_index;
    size_t variants_cnt;
    const char** variants; // strings owned by context->str_buffer
} JsViListItem;

struct JsViListContext {
    StrBuffer str_buffer;

    struct mjs* mjs;
    VariableItemList* list;

    JsViListItem** items;
    size_t items_cnt;

    FuriMessageQueue* input_queue;
    JsEventLoopContract contract;
};

typedef struct {
    int32_t item_index;
    int32_t value_index;
} JsViListEvent;

static mjs_val_t
    input_transformer(struct mjs* mjs, FuriMessageQueue* queue, JsViListContext* context) {
    UNUSED(context);
    JsViListEvent event;
    furi_check(furi_message_queue_get(queue, &event, 0) == FuriStatusOk);

    mjs_val_t event_obj = mjs_mk_object(mjs);
    JS_ASSIGN_MULTI(mjs, event_obj) {
        JS_FIELD("itemIndex", mjs_mk_number(mjs, event.item_index));
        JS_FIELD("valueIndex", mjs_mk_number(mjs, event.value_index));
    }

    return event_obj;
}

static void js_vi_list_change_callback(VariableItem* item) {
    JsViListItem* item_ctx = variable_item_get_context(item);
    JsViListContext* context = item_ctx->context;
    uint8_t item_index = item_ctx->item_index;
    uint8_t value_index = variable_item_get_current_value_index(item);

    // GUI thread: only the C copies are read here, never the live mjs array
    if(value_index < item_ctx->variants_cnt) {
        variable_item_set_current_value_text(item, item_ctx->variants[value_index]);
    }

    JsViListEvent event = {
        .item_index = item_index,
        .value_index = value_index,
    };
    (void)furi_message_queue_put(context->input_queue, &event, 0); // drop on full instead of rebooting the device
}

static bool js_vi_list_add_child(
    struct mjs* mjs,
    VariableItemList* list,
    JsViListContext* context,
    mjs_val_t child_obj) {
    static const JsValueDeclaration js_vi_list_string = JS_VALUE_SIMPLE(JsValueTypeString);
    static const JsValueDeclaration js_vi_list_arr = JS_VALUE_SIMPLE(JsValueTypeAnyArray);
    static const JsValueDeclaration js_vi_list_int_default_0 =
        JS_VALUE_SIMPLE_W_DEFAULT(JsValueTypeInt32, int32_val, 0);

    static const JsValueObjectField js_vi_list_child_fields[] = {
        {"label", &js_vi_list_string},
        {"variants", &js_vi_list_arr},
        {"defaultSelected", &js_vi_list_int_default_0},
    };
    static const JsValueDeclaration js_vi_list_child =
        JS_VALUE_OBJECT_W_DEFAULTS(js_vi_list_child_fields);

    JsValueParseStatus status;
    const char* label;
    mjs_val_t variants;
    int32_t default_selected;
    JS_VALUE_PARSE(
        mjs,
        JS_VALUE_PARSE_SOURCE_VALUE(&js_vi_list_child),
        JsValueParseFlagReturnOnError,
        &status,
        &child_obj,
        &label,
        &variants,
        &default_selected);
    if(status != JsValueParseStatusOk) return false;

    size_t variants_cnt = mjs_array_length(mjs, variants);
    for(size_t i = 0; i < variants_cnt; i++)
        if(!mjs_is_string(mjs_array_get(mjs, variants, i))) {
            mjs_prepend_errorf(mjs, MJS_BAD_ARGS_ERROR, "variants must be strings");
            return false;
        }
    // Item value count is uint8_t and the default must index an existing variant
    if(variants_cnt < 1 || variants_cnt > 255) {
        mjs_prepend_errorf(mjs, MJS_BAD_ARGS_ERROR, "variants must have 1..255 strings");
        return false;
    }
    if(default_selected < 0 || (size_t)default_selected >= variants_cnt) {
        mjs_prepend_errorf(mjs, MJS_BAD_ARGS_ERROR, "defaultSelected out of range");
        return false;
    }

    // Item index is stored as uint8_t; refuse more items than that
    if(context->items_cnt >= 255) {
        mjs_prepend_errorf(mjs, MJS_BAD_ARGS_ERROR, "too many items (max 255)");
        return false;
    }

    // Copy variant strings into C memory owned by the item so the GUI thread never reads mjs
    JsViListItem* item_ctx = malloc(sizeof(JsViListItem));
    item_ctx->context = context;
    item_ctx->item_index = (uint8_t)context->items_cnt;
    item_ctx->variants_cnt = variants_cnt;
    item_ctx->variants = malloc(variants_cnt * sizeof(const char*));
    for(size_t i = 0; i < variants_cnt; i++) {
        mjs_val_t variant = mjs_array_get(mjs, variants, i);
        item_ctx->variants[i] =
            str_buffer_make_owned_clone(&context->str_buffer, mjs_get_string(mjs, &variant, NULL));
    }

    context->items = realloc(context->items, (context->items_cnt + 1) * sizeof(JsViListItem*));
    context->items[context->items_cnt++] = item_ctx;

    VariableItem* item = variable_item_list_add(
        list,
        str_buffer_make_owned_clone(&context->str_buffer, label),
        variants_cnt,
        js_vi_list_change_callback,
        item_ctx);
    variable_item_set_current_value_index(item, default_selected);
    variable_item_set_current_value_text(item, item_ctx->variants[default_selected]);

    return true;
}

static void js_vi_list_free_items(JsViListContext* context) {
    for(size_t i = 0; i < context->items_cnt; i++) {
        free(context->items[i]->variants);
        free(context->items[i]);
    }
    free(context->items);
    context->items = NULL;
    context->items_cnt = 0;
}

static void js_vi_list_reset_children(VariableItemList* list, JsViListContext* context) {
    variable_item_list_reset(list);
    // Items are gone from the list now, so their contexts can be freed
    js_vi_list_free_items(context);
    str_buffer_clear_all_clones(&context->str_buffer);
}

static JsViListContext* ctx_make(struct mjs* mjs, VariableItemList* list, mjs_val_t view_obj) {
    JsViListContext* context = malloc(sizeof(JsViListContext));
    *context = (JsViListContext){
        .str_buffer = {0},
        .mjs = mjs,
        .list = list,
        .input_queue = furi_message_queue_alloc(1, sizeof(JsViListEvent)),
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
    mjs_set(mjs, view_obj, "valueUpdate", ~0, mjs_mk_foreign(mjs, &context->contract));
    return context;
}

static void ctx_destroy(VariableItemList* input, JsViListContext* context, FuriEventLoop* loop) {
    UNUSED(input);
    furi_event_loop_maybe_unsubscribe(loop, context->input_queue);
    furi_message_queue_free(context->input_queue);
    js_vi_list_free_items(context);
    str_buffer_clear_all_clones(&context->str_buffer);
    free(context);
}

static const JsViewDescriptor view_descriptor = {
    .alloc = (JsViewAlloc)variable_item_list_alloc,
    .free = (JsViewFree)variable_item_list_free,
    .get_view = (JsViewGetView)variable_item_list_get_view,
    .custom_make = (JsViewCustomMake)ctx_make,
    .custom_destroy = (JsViewCustomDestroy)ctx_destroy,
    .add_child = (JsViewAddChild)js_vi_list_add_child,
    .reset_children = (JsViewResetChildren)js_vi_list_reset_children,
    .prop_cnt = 0,
    .props = {},
};

JS_GUI_VIEW_DEF(vi_list, &view_descriptor);
