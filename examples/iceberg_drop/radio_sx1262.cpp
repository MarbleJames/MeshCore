#include "radio_sx1262.h"

#include "config.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>

#ifndef D0
#define D0 0
#define D1 1
#define D2 2
#define D3 3
#define D4 4
#define D5 5
#define D8 8
#define D9 9
#define D10 10
#endif

static Module g_mod(D4, D1, D2, D3, SPI);
static SX1262 g_radio(&g_mod);
static bool g_up = false;

bool iceberg_radio_begin(bool rx_boosted) {
  SPI.setPins(D9, D8, D10);
  SPI.begin();

  int16_t state = g_radio.begin(ICEBERG_LORA_FREQ, ICEBERG_LORA_BW, ICEBERG_LORA_SF,
                                ICEBERG_LORA_CR, ICEBERG_LORA_SYNC, ICEBERG_LORA_PWR,
                                ICEBERG_LORA_PREAMBLE, 1.8F, false);
  if (state != RADIOLIB_ERR_NONE) {
    state = g_radio.begin(ICEBERG_LORA_FREQ, ICEBERG_LORA_BW, ICEBERG_LORA_SF,
                          ICEBERG_LORA_CR, ICEBERG_LORA_SYNC, ICEBERG_LORA_PWR,
                          ICEBERG_LORA_PREAMBLE, 0.0F, false);
  }
  if (state != RADIOLIB_ERR_NONE) return false;

  g_radio.setCRC(true);
  g_radio.setCurrentLimit(ICEBERG_LORA_CURRENT_MA);
  g_radio.setDio2AsRfSwitch(true);
  g_radio.setRxBoostedGainMode(rx_boosted);
  g_radio.setRfSwitchPins(D5, RADIOLIB_NC);
  g_up = true;
  return true;
}

void iceberg_radio_sleep() {
  if (!g_up) return;
  // Warm sleep keeps the image calibration and the modem settings.
  // Cold sleep is a few hundred nanoamps lower and forces a full
  // reconfigure, which RadioLib will not do on the next transmit.
  // DIO3 drops the TCXO either way.
  g_radio.sleep(true);
}

int iceberg_radio_send(const uint8_t *data, size_t len) {
  if (!g_up) return -1;
  int16_t state = g_radio.transmit((uint8_t *)data, len);
  iceberg_radio_sleep();
  return state;
}

void iceberg_radio_on_rx(void (*cb)()) {
  g_radio.setPacketReceivedAction(cb);
}

bool iceberg_radio_start_rx() {
  if (!g_up) return false;
  return g_radio.startReceive() == RADIOLIB_ERR_NONE;
}

int iceberg_radio_read(uint8_t *data, size_t cap, size_t *got) {
  size_t len = g_radio.getPacketLength();
  if (len > cap) len = cap;
  int16_t state = g_radio.readData(data, len);
  if (got) *got = (state == RADIOLIB_ERR_NONE) ? len : 0;
  return state;
}

float iceberg_radio_rssi() { return g_radio.getRSSI(); }
float iceberg_radio_snr() { return g_radio.getSNR(); }
