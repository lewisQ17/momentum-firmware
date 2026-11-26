#include "../nfc_eink_app_i.h"
#include "../nfc_eink_screen/nfc_eink_screen.h"
#include <path.h>

static void nfc_eink_text_input_callback(void* context) {
    NfcEinkApp* instance = context;
    view_dispatcher_send_custom_event(
        instance->view_dispatcher, NfcEinkAppCustomEventTextInputDone);
}

void nfc_eink_scene_save_name_on_enter(void* context) {
    NfcEinkApp* instance = context;
    TextInput* text_input = instance->text_input;
    FuriString* folder_path = furi_string_alloc();

    bool name_is_empty = furi_string_empty(instance->file_name);
    if(name_is_empty) {
        furi_string_set(folder_path, NFC_EINK_APP_FOLDER);
        furi_string_set(instance->file_path, NFC_EINK_APP_FOLDER);
    } else {
        path_extract_dirname(furi_string_get_cstr(instance->file_path), folder_path);
    }

    text_input_set_header_text(text_input, "Name the screen");
    text_input_set_result_callback(
        text_input,
        nfc_eink_text_input_callback,
        instance,
        instance->text_store,
        NFC_EINK_NAME_SIZE,
        name_is_empty);

    ValidatorIsFile* validator_is_file = validator_is_file_alloc_init(
        furi_string_get_cstr(folder_path),
        NFC_EINK_APP_EXTENSION,
        furi_string_get_cstr(instance->file_name));
    text_input_set_validator(text_input, validator_is_file_callback, validator_is_file);

    furi_string_free(folder_path);

    view_dispatcher_switch_to_view(instance->view_dispatcher, NfcEinkViewTextInput);
}

bool nfc_eink_scene_save_name_on_event(void* context, SceneManagerEvent event) {
    NfcEinkApp* instance = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == NfcEinkAppCustomEventTextInputDone) {
            // Build the target path fresh on every attempt (a failed save used to append ".eink" twice)
            FuriString* new_path = furi_string_alloc_set(NFC_EINK_APP_FOLDER);
            FuriString* new_name = furi_string_alloc_set(instance->text_store);
            furi_string_cat_str(new_name, NFC_EINK_APP_EXTENSION);
            path_append(new_path, furi_string_get_cstr(new_name));

            if(nfc_eink_screen_save(instance->screen, furi_string_get_cstr(new_path))) {
                // Rename flow: remove the old file only after the new one is safely written
                if(!furi_string_empty(instance->file_name) &&
                   !furi_string_equal(instance->file_path, new_path)) {
                    nfc_eink_screen_delete(furi_string_get_cstr(instance->file_path));
                }
                furi_string_set(instance->file_path, new_path);
                scene_manager_next_scene(instance->scene_manager, NfcEinkAppSceneSaveSuccess);
                dolphin_deed(DolphinDeedNfcSave);
                memset(instance->text_store, 0, sizeof(instance->text_store));
            } else {
                dialog_message_show_storage_error(instance->dialogs, "Save failed");
            }
            furi_string_free(new_path);
            furi_string_free(new_name);
            consumed = true;
        }
    }

    return consumed;
}

void nfc_eink_scene_save_name_on_exit(void* context) {
    NfcEinkApp* instance = context;
    // Leak fix: free the ValidatorIsFile (read before text_input_reset clears the context)
    void* validator_context = text_input_get_validator_callback_context(instance->text_input);
    text_input_set_validator(instance->text_input, NULL, NULL);
    if(validator_context) validator_is_file_free(validator_context);
}
