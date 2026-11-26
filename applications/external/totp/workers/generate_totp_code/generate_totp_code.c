#include "generate_totp_code.h"
#include <furi/core/thread.h>
#include <furi/core/check.h>
#include "../../services/crypto/crypto_facade.h"
#include "../../services/totp/totp.h"
#include "../../services/convert/convert.h"
#include <furi_hal_rtc.h>
#include <memset_s.h>

#define ONE_SEC_MS (1000)

struct TotpGenerateCodeWorkerContext {
    char* code_buffer;
    FuriThread* thread;
    FuriMutex* code_buffer_sync;
    const TokenInfo* token_info;
    float timezone_offset;
    const CryptoSettings* crypto_settings;
    TOTP_NEW_CODE_GENERATED_HANDLER on_new_code_generated_handler;
    void* on_new_code_generated_handler_context;
    TOTP_CODE_LIFETIME_CHANGED_HANDLER on_code_lifetime_changed_handler;
    void* on_code_lifetime_changed_handler_context;
};

static const char STEAM_ALGO_ALPHABET[] = "23456789BCDFGHJKMNPQRTVWXY";

static void
    int_token_to_str(uint64_t i_token_code, char* str, TokenDigitsCount len, TokenHashAlgo algo) {
    char* last_char = str + len;
    *last_char = '\0';
    if(i_token_code == OTP_ERROR) {
        memset(str, '-', len);
    } else {
        if(algo == TokenHashAlgoSteam) {
            char* s = str;
            for(uint8_t i = 0; i < len; i++, s++) {
                *s = STEAM_ALGO_ALPHABET[i_token_code % 26];
                i_token_code = i_token_code / 26;
            }
        } else {
            char* s = --last_char;
            for(int8_t i = len - 1; i >= 0; i--, s--) {
                *s = CONVERT_DIGIT_TO_CHAR(i_token_code % 10);
                i_token_code = i_token_code / 10;
            }
        }
    }
}

static TOTP_ALGO get_totp_algo_impl(TokenHashAlgo algo) {
    switch(algo) {
    case TokenHashAlgoSha1:
    case TokenHashAlgoSteam:
        return TOTP_ALGO_SHA1;
    case TokenHashAlgoSha256:
        return TOTP_ALGO_SHA256;
    case TokenHashAlgoSha512:
        return TOTP_ALGO_SHA512;
    default:
        break;
    }

    return NULL;
}

static void generate_totp_code(
    TotpGenerateCodeWorkerContext* context,
    const TokenInfo* token_info,
    uint32_t current_ts) {
    if(token_info->token != NULL && token_info->token_length > 0) {
        size_t key_length;
        uint8_t* key = totp_crypto_decrypt(
            token_info->token, token_info->token_length, context->crypto_settings, &key_length);
        uint64_t otp_code;
        size_t plain_key_length =
            token_info->token_plain_length > 0 ? token_info->token_plain_length : key_length;
        if(token_info->type == TokenTypeTOTP) {
            otp_code = totp_at(
                get_totp_algo_impl(token_info->algo),
                key,
                plain_key_length,
                current_ts,
                context->timezone_offset,
                token_info->duration);
        } else if(token_info->type == TokenTypeHOTP) {
            otp_code = hotp_at(
                get_totp_algo_impl(token_info->algo), key, plain_key_length, token_info->counter);
        } else {
            otp_code = 0; // unknown type: show zero code instead of crashing
        }

        int_token_to_str(otp_code, context->code_buffer, token_info->digits, token_info->algo);
        memset_s(key, key_length, 0, key_length);
        free(key);
    } else {
        int_token_to_str(0, context->code_buffer, token_info->digits, token_info->algo);
    }
}

// Same shift totp_at applies, so rollover/progress follow the period boundaries of the generated code
static uint64_t apply_timezone_offset(uint64_t ts, float timezone_offset) {
    int32_t offset = (int32_t)(timezone_offset * 3600.0f);
    if(offset > 0) {
        return ts > (uint64_t)offset ? ts - (uint64_t)offset : 0;
    }

    return ts + (uint64_t)(-(int64_t)offset);
}

static int32_t totp_generate_worker_callback(void* context) {
    furi_check(context);

    TotpGenerateCodeWorkerContext* t_context = context;

    // Track the TOTP period instead of testing `ts % duration == 0`, so a skipped RTC second cannot leave a stale code
    uint32_t last_period = UINT32_MAX;
    const TokenInfo* last_token = NULL;
    uint32_t last_duration = 0;

    while(true) {
        uint32_t flags = furi_thread_flags_wait(
            TotpGenerateCodeWorkerEventStop | TotpGenerateCodeWorkerEventForceUpdate,
            FuriFlagWaitAny,
            ONE_SEC_MS);

        if(flags ==
           (uint32_t)
               FuriFlagErrorTimeout) { // If timeout, consider as no error, as we expect this and can handle gracefully
            flags = 0;
        }

        furi_check((flags & FuriFlagError) == 0); //-V562

        if(flags & TotpGenerateCodeWorkerEventStop) break;

        const TokenInfo* token_info = t_context->token_info;
        if(token_info == NULL) {
            continue;
        }

        const bool is_time_based = token_info->type == TokenTypeTOTP;

        uint32_t curr_ts = is_time_based ? furi_hal_rtc_get_timestamp() : 0;
        uint64_t shifted_ts = apply_timezone_offset(curr_ts, t_context->timezone_offset);

        // why: read once so a concurrent token switch cannot give inconsistent values
        const uint32_t duration = token_info->duration;

        // why: the token struct is mutated in place, so a duration change also means a new token
        if(token_info != last_token || (is_time_based && duration != last_duration)) {
            last_token = token_info;
            last_period = UINT32_MAX;
        }

        const bool force_update = (flags & TotpGenerateCodeWorkerEventForceUpdate) != 0;
        const uint32_t period = is_time_based ? (uint32_t)(shifted_ts / duration) : 0;
        const bool period_changed = is_time_based && (period != last_period);
        // Notify "new code" only on a genuine rollover, not on forced/first generation
        const bool time_left = !force_update && period_changed && last_period != UINT32_MAX;
        if(force_update || period_changed) {
            if(furi_mutex_acquire(t_context->code_buffer_sync, FuriWaitForever) == FuriStatusOk) {
                generate_totp_code(t_context, token_info, curr_ts);
                if(is_time_based) {
                    last_period = period;
                    last_duration = duration;
                }
                if(is_time_based) {
                    curr_ts = furi_hal_rtc_get_timestamp();
                    shifted_ts = apply_timezone_offset(curr_ts, t_context->timezone_offset);
                }
                furi_mutex_release(t_context->code_buffer_sync);
                if(t_context->on_new_code_generated_handler != NULL) {
                    (*(t_context->on_new_code_generated_handler))(
                        time_left, t_context->on_new_code_generated_handler_context);
                }
            }
        }

        if(t_context->on_code_lifetime_changed_handler != NULL &&
           token_info->type == TokenTypeTOTP) {
            (*(t_context->on_code_lifetime_changed_handler))(
                (float)(duration - (uint32_t)(shifted_ts % duration)) / (float)duration,
                t_context->on_code_lifetime_changed_handler_context);
        }
    }

    return 0;
}

TotpGenerateCodeWorkerContext* totp_generate_code_worker_start(
    char* code_buffer,
    const TokenInfo* token_info,
    FuriMutex* code_buffer_sync,
    float timezone_offset,
    const CryptoSettings* crypto_settings) {
    TotpGenerateCodeWorkerContext* context = malloc(sizeof(TotpGenerateCodeWorkerContext));
    furi_check(context != NULL);
    context->code_buffer = code_buffer;
    context->token_info = token_info;
    context->code_buffer_sync = code_buffer_sync;
    context->timezone_offset = timezone_offset;
    context->crypto_settings = crypto_settings;
    context->thread = furi_thread_alloc();
    furi_thread_set_name(context->thread, "TOTPGenerateWorker");
    furi_thread_set_stack_size(context->thread, 2048);
    furi_thread_set_context(context->thread, context);
    furi_thread_set_callback(context->thread, totp_generate_worker_callback);
    furi_thread_start(context->thread);
    return context;
}

void totp_generate_code_worker_stop(TotpGenerateCodeWorkerContext* context) {
    furi_check(context != NULL);
    furi_thread_flags_set(furi_thread_get_id(context->thread), TotpGenerateCodeWorkerEventStop);
    furi_thread_join(context->thread);
    furi_thread_free(context->thread);
    free(context);
}

void totp_generate_code_worker_notify(
    TotpGenerateCodeWorkerContext* context,
    TotpGenerateCodeWorkerEvent event) {
    furi_check(context != NULL);
    furi_thread_flags_set(furi_thread_get_id(context->thread), event);
}

void totp_generate_code_worker_set_code_generated_handler(
    TotpGenerateCodeWorkerContext* context,
    TOTP_NEW_CODE_GENERATED_HANDLER on_new_code_generated_handler,
    void* on_new_code_generated_handler_context) {
    furi_check(context != NULL);
    context->on_new_code_generated_handler = on_new_code_generated_handler;
    context->on_new_code_generated_handler_context = on_new_code_generated_handler_context;
}

void totp_generate_code_worker_set_lifetime_changed_handler(
    TotpGenerateCodeWorkerContext* context,
    TOTP_CODE_LIFETIME_CHANGED_HANDLER on_code_lifetime_changed_handler,
    void* on_code_lifetime_changed_handler_context) {
    furi_check(context != NULL);
    context->on_code_lifetime_changed_handler = on_code_lifetime_changed_handler;
    context->on_code_lifetime_changed_handler_context = on_code_lifetime_changed_handler_context;
}
