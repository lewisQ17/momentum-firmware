#include "archive_helpers_ext.h"
#include <momentum/momentum.h>

bool process_favorite_launch(char** args) {
    if(*args && strlen(*args) > 4 && strncmp(*args, "fav/", 4) == 0) {
        *args += 3;
        return true;
    }
    return false;
}

void favorite_timeout_callback(void* _ctx) {
    FavoriteTImeoutCtx* ctx = _ctx;
    while(scene_manager_previous_scene(ctx->scene_manager))
        ;
    view_dispatcher_stop(ctx->view_dispatcher);
}

void favorite_timeout_run(ViewDispatcher* view_dispatcher, SceneManager* scene_manager) {
    uint32_t timeout = momentum_settings.favorite_timeout;
    if(timeout == 0) {
        view_dispatcher_run(view_dispatcher);
        return;
    }

    FavoriteTImeoutCtx ctx = {.view_dispatcher = view_dispatcher, .scene_manager = scene_manager};
    // Event loop timer so the callback runs on the app thread (inside view_dispatcher_run),
    // not on the timer daemon thread racing the dispatcher's own scene handling
    FuriEventLoopTimer* timer = furi_event_loop_timer_alloc(
        view_dispatcher_get_event_loop(view_dispatcher),
        favorite_timeout_callback,
        FuriEventLoopTimerTypeOnce,
        &ctx);
    furi_event_loop_timer_start(timer, timeout * furi_kernel_get_tick_frequency());
    view_dispatcher_run(view_dispatcher);
    furi_event_loop_timer_free(timer);
}
