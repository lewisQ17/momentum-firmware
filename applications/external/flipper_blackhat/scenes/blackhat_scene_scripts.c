#include "../blackhat_app_i.h"

static bool console = false;

static void blackhat_scene_script_list_enter_callback(
    void* context, uint32_t index
)
{
    furi_assert(context);
    BlackhatApp* app = context;

    console = true;
    app->selected_tx_string = RUN_CMD;
    app->selected_option_item_text = app->cmd[index + 1];
    app->text_input_req = false;
    app->selected_menu_index = index;

    FURI_LOG_I("tag/app name", "%s", app->selected_tx_string);

    scene_manager_search_and_switch_to_previous_scene(
        app->scene_manager, BlackhatAppViewConsoleOutput
    );
}

static void blackhat_scene_script_list_change_callback(VariableItem* item)
{
    UNUSED(item);
}

void blackhat_scene_scripts_on_enter(void* context)
{
    BlackhatApp* app = context;
    VariableItemList* var_item_list = app->script_item_list;
    size_t i = 0;
    int start = 0;

    console = false;

    // free every leftover entry from a previous scan (not just up to the new
    // num_scripts) so a rescan with fewer lines does not leak the tail
    for(int j = 0; j < 64; j++) {
        if(app->cmd[j]) {
            free(app->cmd[j]);
            app->cmd[j] = NULL;
        }
    }
    app->num_scripts = 0;

    // stop at cmd[64] to avoid writing past the array
    while (app->script_text[i++] && app->num_scripts < 64) {
        if (app->script_text[i] == '\n') {
            i++;

            // +1 for NUL terminator; the stored line is used as a %s string elsewhere
            app->cmd[app->num_scripts] = malloc(i - start + 1);

            memcpy(
                app->cmd[app->num_scripts], &app->script_text[start], i - start
            );
            app->cmd[app->num_scripts][i - start] = '\0';
            start = i;
            app->num_scripts++;
        }
    }

    variable_item_list_set_enter_callback(
        var_item_list, blackhat_scene_script_list_enter_callback, app
    );

    for (int i = 1; i < app->num_scripts; i++) {
        variable_item_list_add(
            var_item_list,
            app->cmd[i],
            1,
            blackhat_scene_script_list_change_callback,
            app
        );
    }

    view_dispatcher_switch_to_view(
        app->view_dispatcher, BlackhatAppViewScriptItemList
    );
}

bool blackhat_scene_scripts_on_event(void* context, SceneManagerEvent event)
{
    UNUSED(context);
    UNUSED(event);

    return false;
}

void blackhat_scene_scripts_on_exit(void* context)
{
    BlackhatApp* app = context;
    variable_item_list_reset(app->script_item_list);

    if(!console) {
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, BlackhatSceneStart
        );
    }
}
