#pragma once

#include <stdint.h>

// Tiny GGA/RMC consumer for the L76K. Talker id is ignored (GN, GP, GL).
// A sentence with a bad checksum is ignored. Invalid fixes do not clear a
// previous valid one; the caller decides how stale a fix may be.

struct NmeaFix {
  bool valid;
  int32_t lat_e7;
  int32_t lon_e7;
  int16_t alt_dm;     // ICE_ALT_UNKNOWN until a GGA arrives
  uint8_t sats;
  uint8_t hdop_x10;   // 255 until a GGA arrives
  uint32_t unix_s;    // 0 until an RMC date and time arrive
};

void nmea_fix_clear(NmeaFix *fix);

// sentence includes the leading '$' and the "*HH" checksum.
// Returns true when the sentence was well formed (checksum matched).
bool nmea_consume(NmeaFix *fix, const char *sentence);
