/**
 * @file one_wire_slave_i.h
 *
 * Internal 1-Wire slave helpers. Deliberately NOT in SDK_HEADERS, so no api_symbols.csv change.
 */

#pragma once
#include <stdbool.h>

#include "one_wire_slave.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Check whether the last bus operation left an error (reset, timeout, ...)
 * @param [in] bus pointer to OneWireSlave instance
 * @return true if the bus is in an error state
 */
bool onewire_slave_has_error(const OneWireSlave* bus);

#ifdef __cplusplus
}
#endif
