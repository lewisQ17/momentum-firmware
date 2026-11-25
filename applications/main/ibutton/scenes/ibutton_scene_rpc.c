#include "../ibutton_i.h"

// Scene state: 1 once emulation was started by a LoadFile, 0 before
void ibutton_scene_rpc_on_enter(void* context) {
    iButton* ibutton = context;
    scene_manager_set_scene_state(ibutton->scene_manager, iButtonSceneRpc, 0);
}

static void ibutton_rpc_start_emulation(iButton* ibutton) {
    Popup* popup = ibutton->popup;

    popup_set_header(popup, "iButton", 82, 28, AlignCenter, AlignBottom);
    popup_set_text(popup, ibutton->key_name, 82, 32, AlignCenter, AlignTop);
    popup_set_icon(popup, 2, 14, &I_iButtonKey_49x44);

    view_dispatcher_switch_to_view(ibutton->view_dispatcher, iButtonViewPopup);

    ibutton_worker_emulate_start(ibutton->worker, ibutton->key);

    ibutton_notification_message(ibutton, iButtonNotificationMessageEmulateStart);
    notification_message(ibutton->notifications, &sequence_display_backlight_on);
}

// rpc is cleared on RpcSessionClose; never touch a closed session
static void ibutton_scene_rpc_confirm(iButton* ibutton, bool result) {
    if(ibutton->rpc) {
        rpc_system_app_confirm(ibutton->rpc, result);
    }
}

static void ibutton_scene_rpc_set_error(iButton* ibutton, uint32_t code, const char* text) {
    if(ibutton->rpc) {
        rpc_system_app_set_error_code(ibutton->rpc, code);
        rpc_system_app_set_error_text(ibutton->rpc, text);
    }
}

bool ibutton_scene_rpc_on_event(void* context, SceneManagerEvent event) {
    iButton* ibutton = context;

    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;

        if(event.event == iButtonCustomEventRpcLoadFile) {
            bool result = false;

            if(scene_manager_get_scene_state(ibutton->scene_manager, iButtonSceneRpc)) {
                // Already emulating: reloading would rewrite the key the worker is reading
                ibutton_scene_rpc_set_error(
                    ibutton, RpcAppSystemErrorCodeInternalParse, "Key already loaded");
            } else if(ibutton_load_key(ibutton, false)) {
                ibutton_rpc_start_emulation(ibutton);
                scene_manager_set_scene_state(ibutton->scene_manager, iButtonSceneRpc, 1);
                result = true;
            } else {
                ibutton_scene_rpc_set_error(
                    ibutton, RpcAppSystemErrorCodeParseFile, "Cannot load key file");
            }
            ibutton_scene_rpc_confirm(ibutton, result);
        } else if(event.event == iButtonCustomEventRpcExit) {
            ibutton_scene_rpc_confirm(ibutton, true);
            scene_manager_stop(ibutton->scene_manager);
            view_dispatcher_stop(ibutton->view_dispatcher);

        } else if(event.event == iButtonCustomEventRpcSessionClose) {
            // Clear on the app thread, first and back to back, before anything that can
            // block (see ibutton_rpc_command_callback)
            if(ibutton->rpc) {
                rpc_system_app_set_callback(ibutton->rpc, NULL, NULL);
                ibutton->rpc = NULL;
            }
            scene_manager_stop(ibutton->scene_manager);
            view_dispatcher_stop(ibutton->view_dispatcher);
        }
    }

    return consumed;
}

void ibutton_scene_rpc_on_exit(void* context) {
    iButton* ibutton = context;
    Popup* popup = ibutton->popup;

    popup_set_header(popup, NULL, 0, 0, AlignCenter, AlignBottom);
    popup_set_text(popup, NULL, 0, 0, AlignCenter, AlignTop);
    popup_set_icon(popup, 0, 0, NULL);

    ibutton_notification_message(ibutton, iButtonNotificationMessageBlinkStop);
}
