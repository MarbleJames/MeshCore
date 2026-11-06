#include "config.h"
#include "gps.h"
#include "packet.h"
#include "power.h"
#include "radio_sx1262.h"
#include "sensors.h"

#include <Arduino.h>
#include <nrf.h>
#include <stdio.h>

#include "xiao_pins.h"

static uint32_t g_node = 0;
static uint16_t g_seq = 0;
static bool g_ever_fix = false;
static bool g_burst = false;
static uint32_t g_quiet_since = 0;

static void log_sample(const IcebergSample *sample) {
  if (!usb_present()) return;
  console_attach();
  char line[512];
  iceberg_format_json(sample, line, sizeof(line), ICE_RSSI_NONE, 0, 0);
  Serial.println(line);
}

static void transmit(IcebergSample *sample) {
  uint8_t frame[ICE_PACKET_LEN];
  size_t n = iceberg_encode(sample, frame, sizeof(frame));
  if (n == 0) return;
  if (usb_present()) {
    digitalWrite(LED_BLUE, LOW);
  }
  iceberg_radio_send(frame, n);
  digitalWrite(LED_BLUE, HIGH);
  log_sample(sample);
}

static uint32_t calm_interval_ms() {
  uint32_t secs = usb_present() ? ICEBERG_USB_INTERVAL_SECS : ICEBERG_INTERVAL_SECS;
  return secs * 1000UL;
}

void setup() {
  early_power_setup();
  g_node = NRF_FICR->DEVICEID[0];
  console_attach();
  if (usb_present()) {
    Serial.println("iceberg drop");
    Serial.print("node ");
    Serial.println(g_node, HEX);
  }

  bool imu = imu_begin();
  bool sht = sht_begin();
  gps_hw_init();

  if (!iceberg_radio_begin(false)) {
    if (usb_present()) Serial.println("radio init failed");
    system_off();
  }
  iceberg_radio_sleep();

  if (usb_present()) {
    Serial.print("imu ");
    Serial.print(imu ? "yes" : "no");
    Serial.print(" sht ");
    Serial.println(sht ? "yes" : "no");
  }
}

void loop() {
  console_attach();
  bool motion = false;
  uint32_t cycle_start = millis();

  // gps_wake() is a no-op while a roll is already being tracked.
  gps_wake();

  NmeaFix fix;
  nmea_fix_clear(&fix);
  uint32_t gps_budget = ICEBERG_GPS_COLD_SECS * 1000UL;
  if (g_burst) gps_budget = ICEBERG_GPS_BURST_SECS * 1000UL;
  else if (g_ever_fix) gps_budget = ICEBERG_GPS_HOT_SECS * 1000UL;
  gps_wait(&fix, gps_budget, &motion);
  if (fix.valid) g_ever_fix = true;

  if (imu_motion_pending()) motion = true;

  int16_t temp_cc = 0;
  uint16_t rh_cp = ICE_RH_UNKNOWN;
  uint8_t flags = 0;
  if (sht_present() && sht_read(&temp_cc, &rh_cp)) {
    flags |= ICE_FLAG_ENV;
  } else {
    temp_cc = read_die_temp_cc();
    flags |= ICE_FLAG_DIE_TEMP;
  }

  uint16_t batt = read_batt_mv();
  bool critical = !usb_present() && batt >= ICEBERG_BATT_PRESENT_MV && batt < ICEBERG_BATT_CUTOFF_MV;
  if (critical) flags |= ICE_FLAG_CRITICAL;
  if (fix.valid) flags |= ICE_FLAG_GPS;

  if (motion) {
    g_burst = true;
    g_quiet_since = 0;
    flags |= ICE_FLAG_MOTION;
  } else if (g_burst) {
    flags |= ICE_FLAG_MOTION;
    if (g_quiet_since == 0) g_quiet_since = millis();
  }

  IcebergSample sample;
  sample.node_id = g_node;
  sample.seq = g_seq++;
  sample.flags = flags;
  sample.lat_e7 = fix.valid ? fix.lat_e7 : 0;
  sample.lon_e7 = fix.valid ? fix.lon_e7 : 0;
  sample.alt_dm = fix.valid ? fix.alt_dm : ICE_ALT_UNKNOWN;
  sample.temp_cc = temp_cc;
  sample.rh_cp = rh_cp;
  sample.batt_mv = batt;
  sample.unix_s = fix.valid ? fix.unix_s : 0;
  sample.sats = fix.valid ? fix.sats : 0;
  sample.hdop_x10 = fix.valid ? fix.hdop_x10 : ICE_HDOP_UNKNOWN;

  transmit(&sample);

  if (critical) {
    if (usb_present()) Serial.println("battery empty, system off");
    gps_standby();
    system_off();
  }

  imu_clear();
  delay(2);
  if (imu_motion_pending()) {
    g_burst = true;
    g_quiet_since = 0;
  }

  prepare_sleep();

  if (g_burst) {
    uint32_t elapsed = (uint32_t)(millis() - cycle_start);
    uint32_t want = ICEBERG_MOTION_INTERVAL_SECS * 1000UL;
    bool again = false;
    if (elapsed < want) again = sleep_for_ms(want - elapsed);
    if (again || imu_motion_pending()) {
      g_quiet_since = 0;
    } else if (g_quiet_since != 0 &&
               (uint32_t)(millis() - g_quiet_since) >= (ICEBERG_MOTION_SETTLE_SECS * 1000UL)) {
      g_burst = false;
      gps_standby();
    }
  } else {
    gps_standby();
    if (sleep_for_ms(calm_interval_ms()) || imu_motion_pending()) {
      g_burst = true;
      g_quiet_since = 0;
    }
  }
}
