#include "power.h"

#include "sensors.h"

#include <Arduino.h>
#include <nrf.h>

#include "xiao_pins.h"

static bool g_console = false;

bool usb_present() {
  return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

static void leds_off() {
  // XIAO LEDs are active low. HIGH is off. A glowing LED is about a milliamp.
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);
}

static void mic_off() {
  // PDM mic on the Sense. Leave the clock and data parked so they do not float.
#ifdef PIN_PDM_PWR
  pinMode(PIN_PDM_PWR, OUTPUT);
  digitalWrite(PIN_PDM_PWR, LOW);
  pinMode(PIN_PDM_CLK, OUTPUT);
  digitalWrite(PIN_PDM_CLK, LOW);
  pinMode(PIN_PDM_DIN, OUTPUT);
  digitalWrite(PIN_PDM_DIN, LOW);
#endif
}

static void flash_deep_power_down() {
  // P25Q16H standby is tens of microamps. Deep power-down is about 1 uA.
  // The bootloader may have left QSPI owning the pins.
  if (NRF_QSPI->ENABLE) {
    NRF_QSPI->TASKS_DEACTIVATE = 1;
    delay(2);
    NRF_QSPI->ENABLE = 0;
  }
  const uint32_t cs = PIN_QSPI_CS;
  const uint32_t sck = PIN_QSPI_SCK;
  const uint32_t mosi = PIN_QSPI_IO0;
  pinMode(PIN_QSPI_IO2, OUTPUT);
  pinMode(PIN_QSPI_IO3, OUTPUT);
  digitalWrite(PIN_QSPI_IO2, HIGH);  // WP# released
  digitalWrite(PIN_QSPI_IO3, HIGH);  // HOLD# released
  pinMode(cs, OUTPUT);
  pinMode(sck, OUTPUT);
  pinMode(mosi, OUTPUT);
  digitalWrite(cs, HIGH);
  digitalWrite(sck, LOW);
  delayMicroseconds(2);
  digitalWrite(cs, LOW);
  shiftOut((uint8_t)mosi, (uint8_t)sck, MSBFIRST, 0xB9);
  digitalWrite(cs, HIGH);
}

static void clear_fpu_errata() {
#if (__FPU_USED == 1)
  __set_FPSCR(__get_FPSCR() & ~(0x0000009F));
  (void)__get_FPSCR();
  NVIC_ClearPendingIRQ(FPU_IRQn);
#endif
}

void early_power_setup() {
  // High-voltage mode (battery and USB enter through VDDH). Both converter
  // stages have to be on or the LDO path sits at about a milliamp extra.
  NRF_POWER->DCDCEN = 1;
#if defined(NRF52840_XXAA)
  // REG0 stage. The XIAO feeds battery and USB in through VDDH.
  NRF_POWER->DCDCEN0 = 1;
#endif
  NRF_POWER->TASKS_LOWPWR = 1;

  leds_off();
  mic_off();

  // Leave the divider enabled. Turning VBAT_ENABLE off floats the
  // sense pin up to the raw cell voltage, which can exceed the nRF pin
  // limit. The divider is about 3 uA, not the milliamps a comment in
  // variant.cpp suggests.
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);
  pinMode(PIN_VBAT, INPUT);
  // initVariant selects the 100 mA charge setting. 50 mA is the lower one.
  pinMode(PIN_CHARGING_CURRENT, INPUT);
  analogReadResolution(12);
  analogReference(AR_INTERNAL_3_0);

  flash_deep_power_down();
}

void console_attach() {
  if (!usb_present()) return;
  if (!g_console) {
    Serial.begin(115200);
    g_console = true;
  }
}

void prepare_sleep() {
  leds_off();
  if (!usb_present() && g_console) {
    Serial.flush();
    Serial.end();
    g_console = false;
    NRF_USBD->USBPULLUP = 0;
    NRF_USBD->ENABLE = 0;
  }
  clear_fpu_errata();
}

static volatile bool g_motion_edge = false;

static void on_motion_edge() { g_motion_edge = true; }

bool sleep_for_ms(uint32_t ms) {
  g_motion_edge = false;
  bool armed = imu_present();
  if (armed) {
    attachInterrupt(digitalPinToInterrupt(imu_int_pin()), on_motion_edge, RISING);
  }
  uint32_t start = millis();
  while ((uint32_t)(millis() - start) < ms) {
    if (g_motion_edge) break;
    clear_fpu_errata();
    __SEV();
    __WFE();
    __WFE();
  }
  bool motion = g_motion_edge;
  if (armed) detachInterrupt(digitalPinToInterrupt(imu_int_pin()));
  return motion;
}

uint16_t read_batt_mv() {
  (void)analogRead(PIN_VBAT);
  uint32_t adc = analogRead(PIN_VBAT);
  // Same scaling as XiaoNrf52Board: 12-bit, 3.0 V ref, divider ≈ 3.
  return (uint16_t)((adc * 3.0f * 3.0f) / 4.096f);
}

int16_t read_die_temp_cc() {
  NRF_TEMP->EVENTS_DATARDY = 0;
  NRF_TEMP->TASKS_START = 1;
  uint32_t start = millis();
  while (!NRF_TEMP->EVENTS_DATARDY) {
    if ((uint32_t)(millis() - start) > 5) {
      NRF_TEMP->TASKS_STOP = 1;
      return 0;
    }
  }
  int32_t raw = (int32_t)NRF_TEMP->TEMP;
  NRF_TEMP->EVENTS_DATARDY = 0;
  NRF_TEMP->TASKS_STOP = 1;
  return (int16_t)(raw * 25);
}

void system_off() {
  prepare_sleep();
  imu_power(false);
  // Reset and a USB plug-in recover the board. The IMU is not armed:
  // waking on a roll with an empty cell would brown the pack out.
  NRF_POWER->SYSTEMOFF = 1;
  __DSB();
  while (true) {
    __WFE();
  }
}
