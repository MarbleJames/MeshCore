#include "nmea.h"

#include "packet.h"

#include <string.h>

void nmea_fix_clear(NmeaFix *fix) {
  fix->valid = false;
  fix->lat_e7 = 0;
  fix->lon_e7 = 0;
  fix->alt_dm = ICE_ALT_UNKNOWN;
  fix->sats = 0;
  fix->hdop_x10 = ICE_HDOP_UNKNOWN;
  fix->unix_s = 0;
}

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static int split_fields(char *s, char **fields, int max_fields) {
  int n = 0;
  if (max_fields <= 0) return 0;
  fields[n++] = s;
  for (char *p = s; *p && n < max_fields; p++) {
    if (*p == ',' || *p == '*') {
      char sep = *p;
      *p = 0;
      if (sep == ',') fields[n++] = p + 1;
    }
  }
  return n;
}

static bool ends_with(const char *s, const char *suffix) {
  size_t ns = strlen(s);
  size_t nf = strlen(suffix);
  if (ns < nf) return false;
  return memcmp(s + ns - nf, suffix, nf) == 0;
}

// "ddmm.mmm" or "dddmm.mmm" plus a hemisphere character.
static bool parse_coord(const char *field, char hemi, char positive, int32_t *e7) {
  const char *dot = strchr(field, '.');
  if (!dot) return false;
  int whole = (int)(dot - field);
  if (whole < 3 || whole > 5) return false;
  int deg_digits = whole - 2;
  int32_t deg = 0;
  for (int i = 0; i < deg_digits; i++) {
    if (field[i] < '0' || field[i] > '9') return false;
    deg = deg * 10 + (field[i] - '0');
  }
  if (field[deg_digits] < '0' || field[deg_digits + 1] < '0') return false;
  int32_t minutes_e5 = (field[deg_digits] - '0') * 10 + (field[deg_digits + 1] - '0');
  minutes_e5 *= 100000;
  int place = 10000;
  for (const char *p = dot + 1; *p && place; p++) {
    if (*p < '0' || *p > '9') return false;
    minutes_e5 += (*p - '0') * place;
    place /= 10;
  }
  int32_t value = deg * 10000000L + (int32_t)((minutes_e5 * 5LL) / 3);
  if (hemi != positive) value = -value;
  *e7 = value;
  return hemi == positive || hemi == (positive == 'N' ? 'S' : 'W');
}

static int32_t parse_decimetres(const char *field) {
  if (!field || !field[0]) return ICE_ALT_UNKNOWN;
  int sign = 1;
  const char *p = field;
  if (*p == '-') {
    sign = -1;
    p++;
  }
  int32_t whole = 0;
  while (*p >= '0' && *p <= '9') {
    whole = whole * 10 + (*p - '0');
    p++;
  }
  int frac = 0;
  if (*p == '.') {
    p++;
    if (*p >= '0' && *p <= '9') frac = (*p - '0');
  }
  return (int16_t)(sign * (whole * 10 + frac));
}

static uint8_t parse_hdop_x10(const char *field) {
  if (!field || !field[0]) return ICE_HDOP_UNKNOWN;
  int whole = 0;
  const char *p = field;
  while (*p >= '0' && *p <= '9') {
    whole = whole * 10 + (*p - '0');
    p++;
  }
  int frac = 0;
  if (*p == '.' && p[1] >= '0' && p[1] <= '9') frac = p[1] - '0';
  int v = whole * 10 + frac;
  if (v > 254) v = 254;
  return (uint8_t)v;
}

static int parse_int(const char *field) {
  if (!field || !field[0]) return -1;
  int v = 0;
  for (const char *p = field; *p; p++) {
    if (*p < '0' || *p > '9') break;
    v = v * 10 + (*p - '0');
  }
  return v;
}

// Howard Hinnant, public domain. Days since 1970-01-01.
static int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static bool parse_time(const char *hhmmss, int *hh, int *mm, int *ss) {
  if (!hhmmss || strlen(hhmmss) < 6) return false;
  *hh = (hhmmss[0] - '0') * 10 + (hhmmss[1] - '0');
  *mm = (hhmmss[2] - '0') * 10 + (hhmmss[3] - '0');
  *ss = (hhmmss[4] - '0') * 10 + (hhmmss[5] - '0');
  return *hh < 24 && *mm < 60 && *ss < 61;
}

bool nmea_consume(NmeaFix *fix, const char *sentence) {
  if (!sentence || sentence[0] != '$') return false;
  const char *star = strrchr(sentence, '*');
  if (!star || hex_nibble(star[1]) < 0 || hex_nibble(star[2]) < 0) return false;
  uint8_t expect = (uint8_t)((hex_nibble(star[1]) << 4) | hex_nibble(star[2]));
  uint8_t got = 0;
  for (const char *p = sentence + 1; p < star; p++) got ^= (uint8_t)*p;
  if (got != expect) return false;

  char buf[128];
  size_t n = (size_t)(star - (sentence + 1));
  if (n >= sizeof(buf)) return false;
  memcpy(buf, sentence + 1, n);
  buf[n] = 0;

  char *fields[20];
  int nf = split_fields(buf, fields, 20);
  if (nf < 1) return false;

  if (ends_with(fields[0], "RMC") && nf >= 10) {
    int hh, mm, ss;
    bool time_ok = parse_time(fields[1], &hh, &mm, &ss);
    bool active = fields[2][0] == 'A';
    int32_t lat, lon;
    bool pos_ok = active && parse_coord(fields[3], fields[4][0], 'N', &lat) &&
                  parse_coord(fields[5], fields[6][0], 'E', &lon);
    if (pos_ok) {
      fix->valid = true;
      fix->lat_e7 = lat;
      fix->lon_e7 = lon;
    }
    if (time_ok && strlen(fields[9]) >= 6) {
      int day = (fields[9][0] - '0') * 10 + (fields[9][1] - '0');
      int month = (fields[9][2] - '0') * 10 + (fields[9][3] - '0');
      int yy = (fields[9][4] - '0') * 10 + (fields[9][5] - '0');
      int year = yy >= 80 ? 1900 + yy : 2000 + yy;
      if (month >= 1 && month <= 12 && day >= 1 && day <= 31) {
        int64_t days = days_from_civil(year, (unsigned)month, (unsigned)day);
        fix->unix_s = (uint32_t)(days * 86400 + hh * 3600 + mm * 60 + ss);
      }
    }
    return true;
  }

  if (ends_with(fields[0], "GGA") && nf >= 10) {
    int quality = parse_int(fields[6]);
    int32_t lat, lon;
    bool pos_ok = quality > 0 && parse_coord(fields[2], fields[3][0], 'N', &lat) &&
                  parse_coord(fields[4], fields[5][0], 'E', &lon);
    if (pos_ok) {
      fix->valid = true;
      fix->lat_e7 = lat;
      fix->lon_e7 = lon;
      int sats = parse_int(fields[7]);
      if (sats < 0) sats = 0;
      if (sats > 255) sats = 255;
      fix->sats = (uint8_t)sats;
      fix->hdop_x10 = parse_hdop_x10(fields[8]);
      int32_t alt = parse_decimetres(fields[9]);
      if (alt != ICE_ALT_UNKNOWN && alt <= 32767 && alt >= -32767) {
        fix->alt_dm = (int16_t)alt;
      }
    }
    return true;
  }

  return true;
}
