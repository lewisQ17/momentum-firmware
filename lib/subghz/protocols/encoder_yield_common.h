#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <lib/toolbox/level_duration.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Shared yield for protocols whose encoder instance is laid out as
 * { SubGhzProtocolEncoderBase base; SubGhzProtocolBlockEncoder encoder; ... }.
 * @param context Pointer to such an encoder instance
 * @return LevelDuration 
 */
LevelDuration subghz_protocol_encoder_common_yield(void* context);

#ifdef __cplusplus
}
#endif
