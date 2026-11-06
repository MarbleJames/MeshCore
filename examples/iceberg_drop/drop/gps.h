#pragma once

#include "nmea.h"

#include <stdint.h>

void gps_hw_init();
void gps_wake();
void gps_standby();

// Blocks until a valid fix or timeout_ms. Motion edges during the wait are
// recorded in *motion if non-NULL. GPS current dominates this wait; the
// radio is already asleep.
void gps_wait(NmeaFix *fix, uint32_t timeout_ms, bool *motion);
