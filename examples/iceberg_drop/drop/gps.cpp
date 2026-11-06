#include "gps.h"

#include "packet.h"
#include "sensors.h"

#include <Arduino.h>
#include <stdio.h>

#include "xiao_pins.h"

// L76K WAKE_UP is active low for standby (Quectel and Meshtastic agree).
// High for at least 200 ms after release before the UART is valid.
// D6 is the MCU TX (GPS RX). D7 is the MCU RX (GPS TX). D0 is WAKE_UP.
// Those are the three pins the XIAO L76K header uses. D6/D7 are also the
// only leftover header pins once the Wio-SX1262 is stacked, which is why
// the humidity sensor lives on the NFC pads instead of this bus.

static const uint8_t PIN_GPS_WAKE = 0;
static bool g_running = false;

static void gps_send(const char *body) {
  uint8_t cs = 0;
  for (const char *p = body; *p; p++) cs ^= (uint8_t)*p;
  char line[96];
  snprintf(line, sizeof(line), "$%s*%02X\r\n", body, cs);
  Serial1.print(line);
}

static void gps_uart_on() {
  Serial1.setPins(PIN_SERIAL1_RX, PIN_SERIAL1_TX);
  Serial1.begin(9600);
  uint32_t start = millis();
  while (Serial1.available() == 0 && (uint32_t)(millis() - start) < 20) {
  }
  while (Serial1.available()) Serial1.read();
}

void gps_hw_init() {
  // Hold WAKE_UP high through the module's power-on window, then park it.
  // The first report releases it again. Low is standby.
  pinMode(PIN_GPS_WAKE, OUTPUT);
  digitalWrite(PIN_GPS_WAKE, HIGH);
  delay(250);
  digitalWrite(PIN_GPS_WAKE, LOW);
  pinMode(PIN_SERIAL1_TX, OUTPUT);
  digitalWrite(PIN_SERIAL1_TX, HIGH);
  pinMode(PIN_SERIAL1_RX, INPUT);
  g_running = false;
}

void gps_wake() {
  if (g_running) return;
  digitalWrite(PIN_GPS_WAKE, HIGH);
  delay(250);
  gps_uart_on();
  // GPS + GLONASS. BeiDou is the module default and is the weak
  // constellation at high latitude. Not kept across a full power cut,
  // so it is sent on every wake from standby.
  gps_send("PCAS04,5");
  delay(30);
  gps_send("PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0");
  g_running = true;
}

void gps_standby() {
  if (!g_running) {
    digitalWrite(PIN_GPS_WAKE, LOW);
    return;
  }
  digitalWrite(PIN_GPS_WAKE, LOW);
  Serial1.flush();
  Serial1.end();
  pinMode(PIN_SERIAL1_TX, OUTPUT);
  digitalWrite(PIN_SERIAL1_TX, HIGH);
  pinMode(PIN_SERIAL1_RX, INPUT);
  g_running = false;
}

void gps_wait(NmeaFix *fix, uint32_t timeout_ms, bool *motion) {
  char line[128];
  size_t len = 0;
  uint32_t start = millis();
  uint32_t valid_since = 0;
  while ((uint32_t)(millis() - start) < timeout_ms) {
    if (imu_motion_pending()) {
      if (motion) *motion = true;
    }
    while (Serial1.available()) {
      char c = (char)Serial1.read();
      if (c == '\r') continue;
      if (c == '\n') {
        line[len] = 0;
        if (len > 6) nmea_consume(fix, line);
        len = 0;
      } else if (len + 1 < sizeof(line)) {
        line[len++] = c;
      } else {
        len = 0;
      }
    }
    if (fix->valid && fix->unix_s != 0) {
      if (fix->alt_dm != ICE_ALT_UNKNOWN) return;
      if (valid_since == 0) valid_since = millis();
      else if ((uint32_t)(millis() - valid_since) > 1200) return;
    }
    // The GPS is the load here. Nap between bytes so the core is not spinning.
    delay(20);
  }
}
