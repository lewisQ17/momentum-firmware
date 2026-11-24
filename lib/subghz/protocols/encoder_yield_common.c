#include "encoder_yield_common.h"
#include "base.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"

// Single copy of the yield function that was previously duplicated byte-for-byte in 55
// protocols (saves flash). Every user asserts the layout with a _Static_assert.
LevelDuration subghz_protocol_encoder_common_yield(void* context) {
    // Block encoder sits right after the SubGhzProtocolEncoderBase in every user's instance.
    SubGhzProtocolBlockEncoder* encoder =
        (SubGhzProtocolBlockEncoder*)((uint8_t*)context + sizeof(SubGhzProtocolEncoderBase));

    if(encoder->repeat == 0 || !encoder->is_running) {
        encoder->is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = encoder->upload[encoder->front];

    if(++encoder->front == encoder->size_upload) {
        if(!subghz_block_generic_global.endless_tx) encoder->repeat--;
        encoder->front = 0;
    }

    return ret;
}
