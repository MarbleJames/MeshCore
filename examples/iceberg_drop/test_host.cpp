#include "nmea.h"
#include "packet.h"

#include <stdio.h>
#include <string.h>

static int g_fails = 0;

static void expect(bool ok, const char *msg) {
  if (ok) return;
  fprintf(stderr, "FAIL %s\n", msg);
  g_fails++;
}

static void add_checksum(char *sentence, size_t cap) {
  uint8_t cs = 0;
  for (const char *p = sentence + 1; *p; p++) cs ^= (uint8_t)*p;
  size_t n = strlen(sentence);
  snprintf(sentence + n, cap - n, "*%02X", cs);
}

int main() {
  IcebergSample in;
  memset(&in, 0, sizeof(in));
  in.node_id = 0xA1B2C3D4;
  in.seq = 7;
  in.flags = ICE_FLAG_GPS | ICE_FLAG_ENV | ICE_FLAG_MOTION;
  in.lat_e7 = -428426483;
  in.lon_e7 = 1473084733;
  in.alt_dm = 155;
  in.temp_cc = -425;
  in.rh_cp = 8210;
  in.batt_mv = 3910;
  in.unix_s = 946684800;
  in.sats = 8;
  in.hdop_x10 = 9;

  uint8_t frame[ICE_PACKET_LEN];
  expect(iceberg_encode(&in, frame, sizeof(frame)) == ICE_PACKET_LEN, "encode length");
  frame[10] ^= 0xFF;
  IcebergSample bad;
  expect(!iceberg_decode(frame, sizeof(frame), &bad), "crc rejects a flipped byte");
  frame[10] ^= 0xFF;
  IcebergSample out;
  expect(iceberg_decode(frame, sizeof(frame), &out), "decode");
  expect(out.node_id == in.node_id, "node");
  expect(out.seq == in.seq, "seq");
  expect(out.flags == in.flags, "flags");
  expect(out.lat_e7 == in.lat_e7, "lat");
  expect(out.lon_e7 == in.lon_e7, "lon");
  expect(out.alt_dm == in.alt_dm, "alt");
  expect(out.temp_cc == in.temp_cc, "temp");
  expect(out.rh_cp == in.rh_cp, "rh");
  expect(out.batt_mv == in.batt_mv, "batt");
  expect(out.unix_s == in.unix_s, "unix");
  expect(out.sats == in.sats && out.hdop_x10 == in.hdop_x10, "sats");

  char json[512];
  int n = iceberg_format_json(&in, json, sizeof(json), -915, 82, 1);
  expect(n > 0 && (size_t)n < sizeof(json), "json fits");
  expect(strstr(json, "\"node\":\"a1b2c3d4\"") != NULL, "json node");
  expect(strstr(json, "\"lat\":-42.8426483") != NULL, "json lat");
  expect(strstr(json, "\"temp_c\":-4.25") != NULL, "json temp");
  expect(strstr(json, "\"rh\":82.10") != NULL, "json rh");
  expect(strstr(json, "\"rssi\":-91.50") != NULL, "json rssi");
  expect(strstr(json, "\"src\":\"air\"") != NULL, "json src");

  char rmc[128] = "$GNRMC,000000.00,A,4250.5589,S,14718.5084,E,0.00,0.00,010100,,,A";
  add_checksum(rmc, sizeof(rmc));
  char gga[128] = "$GNGGA,000000.00,4250.5589,S,14718.5084,E,1,08,0.9,15.5,M,0.0,M,,";
  add_checksum(gga, sizeof(gga));

  NmeaFix fix;
  nmea_fix_clear(&fix);
  expect(nmea_consume(&fix, rmc), "rmc checksum");
  expect(fix.valid, "rmc valid");
  expect(fix.lat_e7 == -428426483, "rmc lat");
  expect(fix.lon_e7 == 1473084733, "rmc lon");
  expect(fix.unix_s == 946684800, "rmc unix 2000-01-01");
  expect(nmea_consume(&fix, gga), "gga checksum");
  expect(fix.sats == 8, "gga sats");
  expect(fix.hdop_x10 == 9, "gga hdop");
  expect(fix.alt_dm == 155, "gga alt");

  char badcs[128];
  snprintf(badcs, sizeof(badcs), "%s", rmc);
  badcs[strlen(badcs) - 1] = (badcs[strlen(badcs) - 1] == '0') ? '1' : '0';
  NmeaFix other;
  nmea_fix_clear(&other);
  expect(!nmea_consume(&other, badcs), "bad checksum rejected");
  expect(!other.valid, "bad checksum did not apply");

  char invalid[128] = "$GNRMC,120000.00,V,4250.5589,S,14718.5084,E,0.00,0.00,010100,,,A";
  add_checksum(invalid, sizeof(invalid));
  nmea_fix_clear(&other);
  expect(nmea_consume(&other, invalid), "void rmc is well formed");
  expect(!other.valid, "void rmc is not a fix");

  if (g_fails) {
    fprintf(stderr, "%d failed\n", g_fails);
    return 1;
  }
  printf("ok\n");
  return 0;
}
