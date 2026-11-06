#pragma once

// Fallbacks for the Seeed XIAO nRF52840 / Sense variant. The board package
// usually defines these already; the numbers match Seeed's variant.cpp.

#ifndef LED_RED
#define LED_RED 11
#endif
#ifndef LED_GREEN
#define LED_GREEN 13
#endif
#ifndef LED_BLUE
#define LED_BLUE 12
#endif

#ifndef VBAT_ENABLE
#define VBAT_ENABLE 14
#endif
#ifndef PIN_VBAT
#define PIN_VBAT 32
#endif

#ifndef PIN_SERIAL1_RX
#define PIN_SERIAL1_RX 7
#endif
#ifndef PIN_SERIAL1_TX
#define PIN_SERIAL1_TX 6
#endif

#ifndef PIN_LSM6DS3TR_C_POWER
#define PIN_LSM6DS3TR_C_POWER 15
#endif
#ifndef PIN_LSM6DS3TR_C_INT1
#define PIN_LSM6DS3TR_C_INT1 18
#endif
#ifndef PIN_WIRE1_SDA
#define PIN_WIRE1_SDA 17
#endif
#ifndef PIN_WIRE1_SCL
#define PIN_WIRE1_SCL 16
#endif

#ifndef PIN_PDM_PWR
#define PIN_PDM_PWR 19
#endif
#ifndef PIN_PDM_CLK
#define PIN_PDM_CLK 20
#endif
#ifndef PIN_PDM_DIN
#define PIN_PDM_DIN 21
#endif

#ifndef PIN_QSPI_SCK
#define PIN_QSPI_SCK 24
#endif
#ifndef PIN_QSPI_CS
#define PIN_QSPI_CS 25
#endif
#ifndef PIN_QSPI_IO0
#define PIN_QSPI_IO0 26
#endif
#ifndef PIN_QSPI_IO2
#define PIN_QSPI_IO2 28
#endif
#ifndef PIN_QSPI_IO3
#define PIN_QSPI_IO3 29
#endif

#ifndef PIN_NFC1
#define PIN_NFC1 30
#endif
#ifndef PIN_NFC2
#define PIN_NFC2 31
#endif
