#include "packet.h"

#include <stdio.h>

uint16_t iceberg_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) {
        crc = (uint16_t)((crc << 1) ^ 0x1021);
      } else {
        crc = (uint16_t)(crc << 1);
      }
    }
  }
  return crc;
}

static void wr_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)(v >> 8);
}

static void wr_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)(v >> 24);
}

static uint16_t rd_u16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t iceberg_encode(const IcebergSample *sample, uint8_t *out, size_t cap) {
  if (cap < ICE_PACKET_LEN) return 0;
  out[0] = ICE_MAGIC0;
  out[1] = ICE_MAGIC1;
  out[2] = ICE_VERSION;
  out[3] = sample->flags;
  wr_u32(out + 4, sample->node_id);
  wr_u16(out + 8, sample->seq);
  wr_u32(out + 10, (uint32_t)sample->lat_e7);
  wr_u32(out + 14, (uint32_t)sample->lon_e7);
  wr_u16(out + 18, (uint16_t)sample->alt_dm);
  wr_u16(out + 20, (uint16_t)sample->temp_cc);
  wr_u16(out + 22, sample->rh_cp);
  wr_u16(out + 24, sample->batt_mv);
  wr_u32(out + 26, sample->unix_s);
  out[30] = sample->sats;
  out[31] = sample->hdop_x10;
  wr_u16(out + 32, iceberg_crc16(out, 32));
  return ICE_PACKET_LEN;
}

bool iceberg_decode(const uint8_t *in, size_t len, IcebergSample *sample) {
  if (len < ICE_PACKET_LEN) return false;
  if (in[0] != ICE_MAGIC0 || in[1] != ICE_MAGIC1) return false;
  if (in[2] != ICE_VERSION) return false;
  if (rd_u16(in + 32) != iceberg_crc16(in, 32)) return false;
  sample->flags = in[3];
  sample->node_id = rd_u32(in + 4);
  sample->seq = rd_u16(in + 8);
  sample->lat_e7 = (int32_t)rd_u32(in + 10);
  sample->lon_e7 = (int32_t)rd_u32(in + 14);
  sample->alt_dm = (int16_t)rd_u16(in + 18);
  sample->temp_cc = (int16_t)rd_u16(in + 20);
  sample->rh_cp = rd_u16(in + 22);
  sample->batt_mv = rd_u16(in + 24);
  sample->unix_s = rd_u32(in + 26);
  sample->sats = in[30];
  sample->hdop_x10 = in[31];
  return true;
}

static int fmt_e7(char *dst, size_t cap, int32_t e7) {
  uint32_t mag;
  if (e7 < 0) {
    mag = (uint32_t)(-(int64_t)e7);
  } else {
    mag = (uint32_t)e7;
  }
  return snprintf(dst, cap, "%s%lu.%07lu", e7 < 0 ? "-" : "",
                  (unsigned long)(mag / 10000000UL),
                  (unsigned long)(mag % 10000000UL));
}

static int fmt_centi(char *dst, size_t cap, int32_t centi, int negative_ok) {
  int32_t v = centi;
  const char *sign = "";
  if (v < 0) {
    if (!negative_ok) v = 0;
    else {
      sign = "-";
      v = -v;
    }
  }
  return snprintf(dst, cap, "%s%ld.%02ld", sign, (long)(v / 100), (long)(v % 100));
}

int iceberg_format_json(const IcebergSample *sample, char *out, size_t cap,
                        int16_t rssi_x10, int16_t snr_x10, int from_air) {
  char lat[24];
  char lon[24];
  char alt[24];
  char temp[24];
  char rh[24];
  char hdop[16];
  char rssi[16];
  char snr[16];

  int gps = (sample->flags & ICE_FLAG_GPS) != 0;
  if (gps) {
    fmt_e7(lat, sizeof(lat), sample->lat_e7);
    fmt_e7(lon, sizeof(lon), sample->lon_e7);
  }
  if (sample->alt_dm != ICE_ALT_UNKNOWN) {
    fmt_centi(alt, sizeof(alt), (int32_t)sample->alt_dm * 10, 1);
  }
  fmt_centi(temp, sizeof(temp), sample->temp_cc, 1);
  if (sample->rh_cp != ICE_RH_UNKNOWN) {
    fmt_centi(rh, sizeof(rh), sample->rh_cp, 0);
  }
  if (sample->hdop_x10 != ICE_HDOP_UNKNOWN) {
    snprintf(hdop, sizeof(hdop), "%u.%u", sample->hdop_x10 / 10, sample->hdop_x10 % 10);
  }
  if (rssi_x10 != ICE_RSSI_NONE) {
    fmt_centi(rssi, sizeof(rssi), (int32_t)rssi_x10 * 10, 1);
    fmt_centi(snr, sizeof(snr), (int32_t)snr_x10 * 10, 1);
  }

  return snprintf(
      out, cap,
      "{\"node\":\"%08lx\",\"seq\":%u,\"gps\":%d,\"env\":%d,\"motion\":%d,"
      "\"critical\":%d,\"die_temp\":%d,\"lat\":%s,\"lon\":%s,\"alt_m\":%s,"
      "\"temp_c\":%s,\"rh\":%s,\"batt_mv\":%u,\"unix\":%lu,\"sats\":%u,"
      "\"hdop\":%s,\"rssi\":%s,\"snr\":%s,\"src\":\"%s\"}",
      (unsigned long)sample->node_id, sample->seq,
      gps ? 1 : 0,
      (sample->flags & ICE_FLAG_ENV) ? 1 : 0,
      (sample->flags & ICE_FLAG_MOTION) ? 1 : 0,
      (sample->flags & ICE_FLAG_CRITICAL) ? 1 : 0,
      (sample->flags & ICE_FLAG_DIE_TEMP) ? 1 : 0,
      gps ? lat : "null", gps ? lon : "null",
      sample->alt_dm != ICE_ALT_UNKNOWN ? alt : "null",
      temp,
      sample->rh_cp != ICE_RH_UNKNOWN ? rh : "null",
      sample->batt_mv, (unsigned long)sample->unix_s, sample->sats,
      sample->hdop_x10 != ICE_HDOP_UNKNOWN ? hdop : "null",
      rssi_x10 != ICE_RSSI_NONE ? rssi : "null",
      rssi_x10 != ICE_RSSI_NONE ? snr : "null",
      from_air ? "air" : "usb");
}
