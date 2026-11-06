#pragma once

#include <stddef.h>
#include <stdint.h>

// On-air frame. 34 bytes, little endian, CRC-16/CCITT-FALSE over the
// preceding bytes. Sync word on the radio is 0x1B, not MeshCore's 0x12.
//
//  0  magic 0x1B 0x6E
//  2  version (1)
//  3  flags
//  4  node id
//  8  sequence
// 10  lat * 1e7
// 14  lon * 1e7
// 18  altitude, decimetres. ICE_ALT_UNKNOWN if no fix
// 20  temperature, centi-degrees C. Die temperature if ICE_FLAG_DIE_TEMP
// 22  relative humidity, centi-percent. ICE_RH_UNKNOWN if no sensor
// 24  battery millivolts
// 26  GPS UTC unix seconds, 0 if unknown
// 30  satellites
// 31  HDOP * 10, 255 if unknown
// 32  crc16

static const uint8_t ICE_MAGIC0 = 0x1B;
static const uint8_t ICE_MAGIC1 = 0x6E;
static const uint8_t ICE_VERSION = 1;
static const size_t ICE_PACKET_LEN = 34;

static const uint8_t ICE_FLAG_GPS = 1u << 0;
static const uint8_t ICE_FLAG_ENV = 1u << 1;
static const uint8_t ICE_FLAG_MOTION = 1u << 2;
static const uint8_t ICE_FLAG_CRITICAL = 1u << 3;
static const uint8_t ICE_FLAG_DIE_TEMP = 1u << 4;

static const int16_t ICE_ALT_UNKNOWN = (int16_t)0x8000;
static const uint16_t ICE_RH_UNKNOWN = 0xFFFF;
static const uint8_t ICE_HDOP_UNKNOWN = 255;

struct IcebergSample {
  uint32_t node_id;
  uint16_t seq;
  uint8_t flags;
  int32_t lat_e7;
  int32_t lon_e7;
  int16_t alt_dm;
  int16_t temp_cc;
  uint16_t rh_cp;
  uint16_t batt_mv;
  uint32_t unix_s;
  uint8_t sats;
  uint8_t hdop_x10;
};

uint16_t iceberg_crc16(const uint8_t *data, size_t len);

// Returns ICE_PACKET_LEN, or 0 if cap is too small.
size_t iceberg_encode(const IcebergSample *sample, uint8_t *out, size_t cap);
bool iceberg_decode(const uint8_t *in, size_t len, IcebergSample *sample);

// One JSON object, no trailing newline.
// rssi_x10 / snr_x10 use ICE_RSSI_NONE when the sample did not come off air.
static const int16_t ICE_RSSI_NONE = (int16_t)0x8000;
int iceberg_format_json(const IcebergSample *sample, char *out, size_t cap,
                        int16_t rssi_x10, int16_t snr_x10, int from_air);
