#include "sensors.h"

#include "config.h"

#include <Arduino.h>
#include <Wire.h>
#include <nrf.h>

#include "xiao_pins.h"

static bool g_sht = false;
static bool g_imu = false;
static uint8_t g_imu_addr = 0x6A;
static uint8_t g_imu_on_level = HIGH;

static void nfc_pads_as_gpio() {
  // One-time. The nRF52840 boots with these pads as NFC unless UICR says
  // otherwise, and a GPIO write does not stick until that bit is cleared.
  if ((NRF_UICR->NFCPINS & UICR_NFCPINS_PROTECT_Msk) ==
      (UICR_NFCPINS_PROTECT_NFC << UICR_NFCPINS_PROTECT_Pos)) {
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
    }
    NRF_UICR->NFCPINS = UICR_NFCPINS_PROTECT_Disabled << UICR_NFCPINS_PROTECT_Pos;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
    }
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
    NVIC_SystemReset();
  }
}

static uint8_t crc8_31(const uint8_t *data, int len) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

bool sht_present() { return g_sht; }

bool sht_begin() {
  nfc_pads_as_gpio();
  Wire.setPins(PIN_NFC1, PIN_NFC2);
  Wire.begin();
  Wire.setClock(100000);
  Wire.beginTransmission(0x44);
  Wire.write(0x94);
  if (Wire.endTransmission() != 0) {
    // Park the pads. Floating I2C lines are worse than no humidity reading.
    Wire.end();
    pinMode(PIN_NFC1, INPUT_PULLDOWN);
    pinMode(PIN_NFC2, INPUT_PULLDOWN);
    g_sht = false;
    return false;
  }
  delay(2);
  g_sht = true;
  return true;
}

bool sht_read(int16_t *temp_cc, uint16_t *rh_cp) {
  if (!g_sht) return false;
  Wire.beginTransmission(0x44);
  Wire.write(0xFD);  // high repeatability, one shot, heater stays off
  if (Wire.endTransmission() != 0) return false;
  delay(10);
  if (Wire.requestFrom((uint8_t)0x44, (uint8_t)6) != 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();
  if (crc8_31(b, 2) != b[2] || crc8_31(b + 3, 2) != b[5]) return false;
  uint16_t raw_t = ((uint16_t)b[0] << 8) | b[1];
  uint16_t raw_h = ((uint16_t)b[3] << 8) | b[4];
  int32_t t = -4500 + (17500L * raw_t) / 65535L;
  int32_t h = -600 + (12500L * raw_h) / 65535L;
  if (h < 0) h = 0;
  if (h > 10000) h = 10000;
  *temp_cc = (int16_t)t;
  *rh_cp = (uint16_t)h;
  return true;
}

static bool imu_write(uint8_t reg, uint8_t val) {
  Wire1.beginTransmission(g_imu_addr);
  Wire1.write(reg);
  Wire1.write(val);
  return Wire1.endTransmission() == 0;
}

static bool imu_read(uint8_t reg, uint8_t *val) {
  Wire1.beginTransmission(g_imu_addr);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom(g_imu_addr, (uint8_t)1) != 1) return false;
  *val = Wire1.read();
  return true;
}

static bool imu_whoami() {
  uint8_t id = 0;
  if (!imu_read(0x0F, &id)) return false;
  return id == 0x6A || id == 0x69;
}

bool imu_present() { return g_imu; }
uint32_t imu_int_pin() { return PIN_LSM6DS3TR_C_INT1; }

void imu_power(bool on) {
  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  uint8_t off_level = g_imu_on_level == HIGH ? LOW : HIGH;
  digitalWrite(PIN_LSM6DS3TR_C_POWER, on ? g_imu_on_level : off_level);
}

bool imu_begin() {
  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  pinMode(PIN_LSM6DS3TR_C_INT1, INPUT_PULLDOWN);
  Wire1.setPins(PIN_WIRE1_SDA, PIN_WIRE1_SCL);
  Wire1.begin();
  Wire1.setClock(100000);

  // The Sense schematic gates the IMU with this pin. Try the polarity
  // Seeed uses (high = on), then the other way, so a plain XIAO (no IMU)
  // simply reports "no motion wake" instead of hanging.
  const uint8_t levels[2] = {HIGH, LOW};
  bool found = false;
  for (int i = 0; i < 2 && !found; i++) {
    digitalWrite(PIN_LSM6DS3TR_C_POWER, levels[i]);
    delay(20);
    for (uint8_t addr = 0x6A; addr <= 0x6B; addr++) {
      g_imu_addr = addr;
      if (imu_whoami()) {
        g_imu_on_level = levels[i];
        found = true;
        break;
      }
    }
  }
  if (!found) {
    digitalWrite(PIN_LSM6DS3TR_C_POWER, LOW);
    g_imu = false;
    return false;
  }

  imu_write(0x12, 0x01);  // software reset
  delay(20);
  imu_write(0x12, 0x44);  // BDU + auto increment
  imu_write(0x15, 0x10);  // accelerometer low-power (high-performance off)
  imu_write(0x11, 0x00);  // gyro stays powered down
  imu_write(0x10, 0x10);  // 12.5 Hz, ±2 g. About 10 uA.
  imu_write(0x5B, (uint8_t)(ICEBERG_WAKE_THS & 0x3F));
  imu_write(0x5C, 0x40);  // condition must hold for 2 samples
  imu_write(0x59, 0x60);  // 6D threshold 50 degrees
  imu_write(0x58, 0x8F);  // interrupts on, XYZ, latched
  imu_write(0x5E, 0x24);  // wake-up and 6D both to INT1
  imu_clear();
  g_imu = true;
  return true;
}

void imu_clear() {
  if (!g_imu) return;
  uint8_t dummy = 0;
  imu_read(0x1B, &dummy);
  imu_read(0x1D, &dummy);
}

bool imu_motion_pending() {
  if (!g_imu) return false;
  return digitalRead(PIN_LSM6DS3TR_C_INT1) == HIGH;
}
