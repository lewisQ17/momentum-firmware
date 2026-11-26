#include "py/mphal.h"
#include "py/obj.h"

#include "mp_flipper_modtime.h"

mp_obj_t mp_time_time_get(void) {
    uint32_t timestamp = mp_flipper_get_timestamp();

    return mp_obj_new_int_from_uint(timestamp);
}

uint64_t mp_hal_time_ns(void) {
    // widen first: the 32-bit product overflowed and was not nanoseconds
    return (uint64_t)mp_flipper_get_timestamp() * 1000000000ULL;
}

mp_uint_t mp_hal_ticks_ms(void) {
    // scale the running tick counter, not the constant tick frequency
    return (mp_uint_t)((uint64_t)mp_flipper_get_tick() * 1000ULL / mp_flipper_get_tick_frequency());
}

mp_uint_t mp_hal_ticks_us(void) {
    return (mp_uint_t)((uint64_t)mp_flipper_get_tick() * 1000000ULL / mp_flipper_get_tick_frequency());
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return mp_flipper_get_tick();
}

void mp_hal_delay_ms(mp_uint_t ms) {
    mp_flipper_delay_ms(ms);
}

void mp_hal_delay_us(mp_uint_t us) {
    mp_flipper_delay_us(us);
}
