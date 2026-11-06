#include "packet.h"
#include "radio_sx1262.h"

#include <Arduino.h>

#include "xiao_pins.h"

static volatile bool g_rx = false;

static void on_packet() { g_rx = true; }

void setup() {
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);

  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && (uint32_t)(millis() - start) < 2000) delay(10);
  Serial.println("iceberg gateway");

  if (!iceberg_radio_begin(true)) {
    Serial.println("radio init failed");
    while (true) delay(1000);
  }
  iceberg_radio_on_rx(on_packet);
  if (!iceberg_radio_start_rx()) {
    Serial.println("startReceive failed");
    while (true) delay(1000);
  }
  Serial.println("listening");
}

void loop() {
  if (!g_rx) {
    delay(5);
    return;
  }
  g_rx = false;

  uint8_t buf[64];
  size_t got = 0;
  int state = iceberg_radio_read(buf, sizeof(buf), &got);
  float rssi = iceberg_radio_rssi();
  float snr = iceberg_radio_snr();
  iceberg_radio_start_rx();

  digitalWrite(LED_GREEN, LOW);
  delay(15);
  digitalWrite(LED_GREEN, HIGH);

  if (state != 0) {
    Serial.print("rx error ");
    Serial.println(state);
    return;
  }

  IcebergSample sample;
  if (!iceberg_decode(buf, got, &sample)) {
    Serial.print("drop ");
    Serial.print(got);
    Serial.println(" bytes, not an iceberg frame");
    return;
  }

  int16_t rssi_x10 = (int16_t)(rssi * 10.0f);
  int16_t snr_x10 = (int16_t)(snr * 10.0f);
  char line[512];
  iceberg_format_json(&sample, line, sizeof(line), rssi_x10, snr_x10, 1);
  Serial.println(line);
}
