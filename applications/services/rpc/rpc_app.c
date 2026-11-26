#include "flipper.pb.h"
#include <core/record.h>
#include "rpc_i.h"
#include <furi.h>
#include <loader/loader.h>
#include "rpc_app.h"

#define TAG "RpcSystemApp"

struct RpcAppSystem {
    RpcSession* session;

    RpcAppSystemCallback callback;
    void* callback_context;

    uint32_t error_code;
    char* error_text;

    uint32_t last_command_id;
    RpcAppSystemEventType last_event_type;
};

#define RPC_SYSTEM_APP_TEMP_ARGS_SIZE 16

static void rpc_system_app_send_state_response(
    RpcAppSystem* rpc_app,
    PB_App_AppState state,
    const char* name) {
    PB_Main* response = malloc(sizeof(PB_Main));

    response->which_content = PB_Main_app_state_response_tag;
    response->content.app_state_response.state = state;

    FURI_LOG_D(TAG, "%s", name);
    rpc_send(rpc_app->session, response);

    free(response);
}

static void rpc_system_app_send_error_response(
    RpcAppSystem* rpc_app,
    uint32_t command_id,
    PB_CommandStatus status,
    const char* name) {
    // Not describing all possible errors as only APP_NOT_RUNNING is used
    const char* status_str = status == PB_CommandStatus_ERROR_APP_NOT_RUNNING ? "APP_NOT_RUNNING" :
                             status == PB_CommandStatus_ERROR_BUSY            ? "BUSY" :
                                                                                "UNKNOWN";
    FURI_LOG_E(TAG, "%s: %s, id %lu, status: %d", name, status_str, command_id, status);
    rpc_send_and_release_empty(rpc_app->session, command_id, status);
}

// Returns false (and answers the host with ERROR_BUSY) if a previous command is still unconfirmed.
// Pending state is tracked by event type, not command_id, because command_id 0 is a valid id.
// AppExit is always accepted so the host can still close a stuck app.
static bool rpc_system_app_set_last_command(
    RpcAppSystem* rpc_app,
    uint32_t command_id,
    const RpcAppSystemEvent* event,
    const char* name) {
    if(rpc_app->last_event_type != RpcAppEventTypeInvalid) {
        if(event->type != RpcAppEventTypeAppExit) {
            rpc_system_app_send_error_response(
                rpc_app, command_id, PB_CommandStatus_ERROR_BUSY, name);
            return false;
        }
        FURI_LOG_W(TAG, "%s: overriding pending command", name);
    }

    rpc_app->last_command_id = command_id;
    rpc_app->last_event_type = event->type;
    return true;
}

static void rpc_system_app_start_process(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(request->which_content == PB_Main_app_start_request_tag);

    RpcAppSystem* rpc_app = context;
    furi_assert(rpc_app);

    FURI_LOG_D(TAG, "StartProcess: id %lu", request->command_id);

    Loader* loader = furi_record_open(RECORD_LOADER);
    const char* app_name = request->content.app_start_request.name;

    PB_CommandStatus result;

    if(app_name) {
        rpc_system_app_error_reset(rpc_app);

        char app_args_temp[RPC_SYSTEM_APP_TEMP_ARGS_SIZE];
        const char* app_args = request->content.app_start_request.args;

        bool args_forged = false;
        if(app_args && strcmp(app_args, "RPC") == 0) {
            // If app is being started in RPC mode - pass RPC context via args string
            snprintf(app_args_temp, RPC_SYSTEM_APP_TEMP_ARGS_SIZE, "RPC %08lX", (uint32_t)rpc_app);
            app_args = app_args_temp;
        } else if(app_args && strncmp(app_args, "RPC", 3) == 0) {
            // Apps parse "RPC <hex>" as a pointer: a client must never supply it
            args_forged = true;
        }

        const LoaderStatus status = args_forged ?
                                        LoaderStatusErrorUnknownApp :
                                        loader_start(loader, app_name, app_args, NULL);
        if(status == LoaderStatusErrorAppStarted) {
            result = PB_CommandStatus_ERROR_APP_SYSTEM_LOCKED;
        } else if(status == LoaderStatusErrorInternal) {
            result = PB_CommandStatus_ERROR_APP_CANT_START;
        } else if(status == LoaderStatusErrorApiMismatchCanceled) {
            // User pressed Cancel on the on-device API mismatch dialog
            result = PB_CommandStatus_ERROR_APP_CANT_START;
        } else if(status == LoaderStatusErrorUnknownApp) {
            result = PB_CommandStatus_ERROR_INVALID_PARAMETERS;
        } else if(status == LoaderStatusOk) {
            result = PB_CommandStatus_OK;
        } else {
            // Never crash on a host-triggerable loader status
            result = PB_CommandStatus_ERROR_APP_CANT_START;
        }
    } else {
        result = PB_CommandStatus_ERROR_INVALID_PARAMETERS;
    }

    furi_record_close(RECORD_LOADER);

    FURI_LOG_D(TAG, "StartProcess: response id %lu, result %d", request->command_id, result);
    rpc_send_and_release_empty(rpc_app->session, request->command_id, result);
}

static void rpc_system_app_lock_status_process(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(request->which_content == PB_Main_app_lock_status_request_tag);

    RpcAppSystem* rpc_app = context;
    furi_assert(rpc_app);

    rpc_system_app_error_reset(rpc_app);

    FURI_LOG_D(TAG, "LockStatus");

    PB_Main* response = malloc(sizeof(PB_Main));

    response->command_id = request->command_id;
    response->which_content = PB_Main_app_lock_status_response_tag;

    Loader* loader = furi_record_open(RECORD_LOADER);
    response->content.app_lock_status_response.locked = loader_is_locked(loader);
    furi_record_close(RECORD_LOADER);

    FURI_LOG_D(TAG, "LockStatus: response");
    rpc_send_and_release(rpc_app->session, response);

    free(response);
}

static void rpc_system_app_exit_request(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(request->which_content == PB_Main_app_exit_request_tag);

    RpcAppSystem* rpc_app = context;
    furi_assert(rpc_app);

    // Snapshot once: the app thread may clear the callback concurrently on exit
    RpcAppSystemCallback callback = rpc_app->callback;
    void* callback_context = rpc_app->callback_context;
    if(callback) {
        FURI_LOG_D(TAG, "ExitRequest: id %lu", request->command_id);

        const RpcAppSystemEvent event = {
            .type = RpcAppEventTypeAppExit,
            .data =
                {
                    .type = RpcAppSystemEventDataTypeNone,
                    {0},
                },
        };

        if(!rpc_system_app_set_last_command(rpc_app, request->command_id, &event, "ExitRequest")) {
            return;
        }
        rpc_system_app_error_reset(rpc_app);

        callback(&event, callback_context);

    } else {
        rpc_system_app_send_error_response(
            rpc_app, request->command_id, PB_CommandStatus_ERROR_APP_NOT_RUNNING, "ExitRequest");
    }
}

static void rpc_system_app_load_file(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(request->which_content == PB_Main_app_load_file_request_tag);

    RpcAppSystem* rpc_app = context;
    furi_assert(rpc_app);

    // Snapshot once: the app thread may clear the callback concurrently on exit
    RpcAppSystemCallback callback = rpc_app->callback;
    void* callback_context = rpc_app->callback_context;
    if(callback) {
        FURI_LOG_D(TAG, "LoadFile: id %lu", request->command_id);

        const RpcAppSystemEvent event = {
            .type = RpcAppEventTypeLoadFile,
            .data =
                {
                    .type = RpcAppSystemEventDataTypeString,
                    .string = request->content.app_load_file_request.path,
                },
        };

        if(!rpc_system_app_set_last_command(rpc_app, request->command_id, &event, "LoadFile")) {
            return;
        }
        rpc_system_app_error_reset(rpc_app);

        callback(&event, callback_context);

    } else {
        rpc_system_app_send_error_response(
            rpc_app, request->command_id, PB_CommandStatus_ERROR_APP_NOT_RUNNING, "LoadFile");
    }
}

static void rpc_system_app_button_press(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(request->which_content == PB_Main_app_button_press_request_tag);

    RpcAppSystem* rpc_app = context;
    furi_assert(rpc_app);

    // Snapshot once: the app thread may clear the callback concurrently on exit
    RpcAppSystemCallback callback = rpc_app->callback;
    void* callback_context = rpc_app->callback_context;
    if(callback) {
        FURI_LOG_D(TAG, "ButtonPress");

        RpcAppSystemEvent event;
        event.type = RpcAppEventTypeButtonPress;

        if(strlen(request->content.app_button_press_request.args) != 0) {
            event.data.type = RpcAppSystemEventDataTypeString;
            event.data.string = request->content.app_button_press_request.args;
        } else {
            event.data.type = RpcAppSystemEventDataTypeInt32;
            event.data.i32 = request->content.app_button_press_request.index;
        }

        if(!rpc_system_app_set_last_command(rpc_app, request->command_id, &event, "ButtonPress")) {
            return;
        }
        rpc_system_app_error_reset(rpc_app);

        callback(&event, callback_context);

    } else {
        rpc_system_app_send_error_response(
            rpc_app, request->command_id, PB_CommandStatus_ERROR_APP_NOT_RUNNING, "ButtonPress");
    }
}

static void rpc_system_app_button_release(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(request->which_content == PB_Main_app_button_release_request_tag);

    RpcAppSystem* rpc_app = context;
    furi_assert(rpc_app);

    // Snapshot once: the app thread may clear the callback concurrently on exit
    RpcAppSystemCallback callback = rpc_app->callback;
    void* callback_context = rpc_app->callback_context;
    if(callback) {
        FURI_LOG_D(TAG, "ButtonRelease");

        const RpcAppSystemEvent event = {
            .type = RpcAppEventTypeButtonRelease,
            .data =
                {
                    .type = RpcAppSystemEventDataTypeNone,
                    {0},
                },
        };

        if(!rpc_system_app_set_last_command(
               rpc_app, request->command_id, &event, "ButtonRelease")) {
            return;
        }
        rpc_system_app_error_reset(rpc_app);

        callback(&event, callback_context);

    } else {
        rpc_system_app_send_error_response(
            rpc_app, request->command_id, PB_CommandStatus_ERROR_APP_NOT_RUNNING, "ButtonRelease");
    }
}

static void rpc_system_app_button_press_release(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(request->which_content == PB_Main_app_button_press_release_request_tag);

    RpcAppSystem* rpc_app = context;
    furi_assert(rpc_app);

    // Snapshot once: the app thread may clear the callback concurrently on exit
    RpcAppSystemCallback callback = rpc_app->callback;
    void* callback_context = rpc_app->callback_context;
    if(callback) {
        FURI_LOG_D(TAG, "ButtonPressRelease");

        RpcAppSystemEvent event;
        event.type = RpcAppEventTypeButtonPressRelease;

        if(strlen(request->content.app_button_press_release_request.args) != 0) {
            event.data.type = RpcAppSystemEventDataTypeString;
            event.data.string = request->content.app_button_press_release_request.args;
        } else {
            event.data.type = RpcAppSystemEventDataTypeInt32;
            event.data.i32 = request->content.app_button_press_release_request.index;
        }

        if(!rpc_system_app_set_last_command(
               rpc_app, request->command_id, &event, "ButtonPressRelease")) {
            return;
        }
        rpc_system_app_error_reset(rpc_app);

        callback(&event, callback_context);

    } else {
        rpc_system_app_send_error_response(
            rpc_app,
            request->command_id,
            PB_CommandStatus_ERROR_APP_NOT_RUNNING,
            "ButtonPressRelease");
    }
}

static void rpc_system_app_get_error_process(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(request->which_content == PB_Main_app_get_error_request_tag);

    RpcAppSystem* rpc_app = context;
    furi_assert(rpc_app);

    PB_Main* response = malloc(sizeof(PB_Main));

    response->command_id = request->command_id;
    response->which_content = PB_Main_app_get_error_response_tag;
    response->content.app_get_error_response.code = rpc_app->error_code;
    response->content.app_get_error_response.text = rpc_app->error_text;

    FURI_LOG_D(TAG, "GetError");
    rpc_send(rpc_app->session, response);

    free(response);
}

static void rpc_system_app_data_exchange_process(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(request->which_content == PB_Main_app_data_exchange_request_tag);

    RpcAppSystem* rpc_app = context;
    furi_assert(rpc_app);

    // Snapshot once: the app thread may clear the callback concurrently on exit
    RpcAppSystemCallback callback = rpc_app->callback;
    void* callback_context = rpc_app->callback_context;
    if(callback) {
        FURI_LOG_D(TAG, "DataExchange");

        const pb_bytes_array_t* data = request->content.app_data_exchange_request.data;

        const RpcAppSystemEvent event = {
            .type = RpcAppEventTypeDataExchange,
            .data =
                {
                    .type = RpcAppSystemEventDataTypeBytes,
                    .bytes =
                        {
                            .ptr = data ? data->bytes : NULL,
                            .size = data ? data->size : 0,
                        },
                },
        };

        if(!rpc_system_app_set_last_command(rpc_app, request->command_id, &event, "DataExchange")) {
            return;
        }
        rpc_system_app_error_reset(rpc_app);

        callback(&event, callback_context);
    } else {
        rpc_system_app_send_error_response(
            rpc_app, request->command_id, PB_CommandStatus_ERROR_APP_NOT_RUNNING, "DataExchange");
    }
}

void rpc_system_app_send_started(RpcAppSystem* rpc_app) {
    furi_check(rpc_app);
    rpc_system_app_send_state_response(rpc_app, PB_App_AppState_APP_STARTED, "SendStarted");
}

void rpc_system_app_send_exited(RpcAppSystem* rpc_app) {
    furi_check(rpc_app);
    rpc_system_app_send_state_response(rpc_app, PB_App_AppState_APP_CLOSED, "SendExit");
}

void rpc_system_app_confirm(RpcAppSystem* rpc_app, bool result) {
    furi_check(rpc_app);
    // Surplus confirm (host-controlled timing) must not crash the device
    if(rpc_app->last_event_type == RpcAppEventTypeInvalid) {
        FURI_LOG_W(TAG, "Confirm without pending command");
        return;
    }

    const uint32_t last_command_id = rpc_app->last_command_id;
    const RpcAppSystemEventType last_event_type = rpc_app->last_event_type;

    rpc_app->last_command_id = 0;
    rpc_app->last_event_type = RpcAppEventTypeInvalid;

    const PB_CommandStatus status = result ? PB_CommandStatus_OK :
                                             PB_CommandStatus_ERROR_APP_CMD_ERROR;
    FURI_LOG_D(
        TAG,
        "AppConfirm: event %d last_id %lu status %d",
        last_event_type,
        last_command_id,
        status);

    rpc_send_and_release_empty(rpc_app->session, last_command_id, status);
}

void rpc_system_app_set_callback(RpcAppSystem* rpc_app, RpcAppSystemCallback callback, void* ctx) {
    furi_check(rpc_app);

    // Drop stale pending command of a previous/dead app so it can't poison the session
    rpc_app->last_command_id = 0;
    rpc_app->last_event_type = RpcAppEventTypeInvalid;

    // callback last: rpc_system_app_free() frees us as soon as it reads NULL
    // keep ctx when clearing: handlers snapshot callback+ctx and must not see NULL ctx
    if(callback) rpc_app->callback_context = ctx;
    rpc_app->callback = callback;
}

void rpc_system_app_set_error_code(RpcAppSystem* rpc_app, uint32_t error_code) {
    furi_check(rpc_app);
    rpc_app->error_code = error_code;
}

void rpc_system_app_set_error_text(RpcAppSystem* rpc_app, const char* error_text) {
    furi_check(rpc_app);

    if(rpc_app->error_text) {
        free(rpc_app->error_text);
    }

    rpc_app->error_text = error_text ? strdup(error_text) : NULL;
}

void rpc_system_app_error_reset(RpcAppSystem* rpc_app) {
    furi_check(rpc_app);

    rpc_system_app_set_error_code(rpc_app, 0);
    rpc_system_app_set_error_text(rpc_app, NULL);
}

void rpc_system_app_exchange_data(RpcAppSystem* rpc_app, const uint8_t* data, size_t data_size) {
    furi_check(rpc_app);

    PB_Main* request = malloc(sizeof(PB_Main));

    request->which_content = PB_Main_app_data_exchange_request_tag;
    PB_App_DataExchangeRequest* content = &request->content.app_data_exchange_request;

    if(data && data_size) {
        content->data = malloc(PB_BYTES_ARRAY_T_ALLOCSIZE(data_size));
        content->data->size = data_size;
        memcpy(content->data->bytes, data, data_size);
    } else {
        content->data = NULL;
    }

    rpc_send_and_release(rpc_app->session, request);

    free(request);
}

void* rpc_system_app_alloc(RpcSession* session) {
    furi_assert(session);

    RpcAppSystem* rpc_app = malloc(sizeof(RpcAppSystem));
    rpc_app->session = session;
    rpc_app->error_text = NULL; // freed in rpc_system_app_free()

    RpcHandler rpc_handler = {
        .message_handler = NULL,
        .decode_submessage = NULL,
        .context = rpc_app,
    };

    rpc_handler.message_handler = rpc_system_app_start_process;
    rpc_add_handler(session, PB_Main_app_start_request_tag, &rpc_handler);

    rpc_handler.message_handler = rpc_system_app_lock_status_process;
    rpc_add_handler(session, PB_Main_app_lock_status_request_tag, &rpc_handler);

    rpc_handler.message_handler = rpc_system_app_exit_request;
    rpc_add_handler(session, PB_Main_app_exit_request_tag, &rpc_handler);

    rpc_handler.message_handler = rpc_system_app_load_file;
    rpc_add_handler(session, PB_Main_app_load_file_request_tag, &rpc_handler);

    rpc_handler.message_handler = rpc_system_app_button_press;
    rpc_add_handler(session, PB_Main_app_button_press_request_tag, &rpc_handler);

    rpc_handler.message_handler = rpc_system_app_button_release;
    rpc_add_handler(session, PB_Main_app_button_release_request_tag, &rpc_handler);

    rpc_handler.message_handler = rpc_system_app_button_press_release;
    rpc_add_handler(session, PB_Main_app_button_press_release_request_tag, &rpc_handler);

    rpc_handler.message_handler = rpc_system_app_get_error_process;
    rpc_add_handler(session, PB_Main_app_get_error_request_tag, &rpc_handler);

    rpc_handler.message_handler = rpc_system_app_data_exchange_process;
    rpc_add_handler(session, PB_Main_app_data_exchange_request_tag, &rpc_handler);

    return rpc_app;
}

void rpc_system_app_free(void* context) {
    RpcAppSystem* rpc_app = context;
    furi_assert(rpc_app);
    furi_assert(rpc_app->session);

    if(rpc_app->callback) {
        const RpcAppSystemEvent event = {
            .type = RpcAppEventTypeSessionClose,
            .data =
                {
                    .type = RpcAppSystemEventDataTypeNone,
                    {0},
                },
        };

        rpc_app->callback(&event, rpc_app->callback_context);
    }

    while(rpc_app->callback) {
        furi_delay_tick(1);
    }

    // callback is NULL now, nobody can set error_text anymore
    free(rpc_app->error_text);
    free(rpc_app);
}
