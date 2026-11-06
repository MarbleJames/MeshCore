#pragma once

// XIAO nRF52840 Sense + Wio-SX1262 + L76K drop node.
// Calm reports are an hour apart. USB power uses a short interval so a
// bench setup can be checked without waiting. A large roll or jolt switches
// to the fast interval until the IMU has been quiet for the settle time.

#ifndef ICEBERG_INTERVAL_SECS
#define ICEBERG_INTERVAL_SECS 3600
#endif

#ifndef ICEBERG_USB_INTERVAL_SECS
#define ICEBERG_USB_INTERVAL_SECS 60
#endif

#ifndef ICEBERG_MOTION_INTERVAL_SECS
#define ICEBERG_MOTION_INTERVAL_SECS 10
#endif

#ifndef ICEBERG_MOTION_SETTLE_SECS
#define ICEBERG_MOTION_SETTLE_SECS 60
#endif

// Hot window is used after the module has already had a fix this boot
// (standby keeps the ephemeris). Cold window covers the first search.
#ifndef ICEBERG_GPS_HOT_SECS
#define ICEBERG_GPS_HOT_SECS 25
#endif

#ifndef ICEBERG_GPS_COLD_SECS
#define ICEBERG_GPS_COLD_SECS 90
#endif

// While a roll is in progress the receiver is already tracking.
#ifndef ICEBERG_GPS_BURST_SECS
#define ICEBERG_GPS_BURST_SECS 8
#endif

// Stop transmitting and cut the MCU off once the cell is actually empty.
// Reads below ICEBERG_BATT_PRESENT_MV are treated as "no cell", so a USB
// bench setup with nothing on the battery pads does not shut itself down.
#ifndef ICEBERG_BATT_CUTOFF_MV
#define ICEBERG_BATT_CUTOFF_MV 3200
#endif

#ifndef ICEBERG_BATT_PRESENT_MV
#define ICEBERG_BATT_PRESENT_MV 2500
#endif

// LSM6DS3TR-C wake-up threshold, 1 LSB = FS/64. At ±2 g, 8 = 250 mg.
// 6D orientation uses the most sensitive step the chip offers (50 degrees).
#ifndef ICEBERG_WAKE_THS
#define ICEBERG_WAKE_THS 8
#endif

// Same band MeshCore uses in Europe, different sync word so the two
// protocols do not parse each other's packets. Airtime still collides
// if a busy mesh is on this frequency; change the freq if that matters.
#ifndef ICEBERG_LORA_FREQ
#define ICEBERG_LORA_FREQ 869.618F
#endif

#ifndef ICEBERG_LORA_BW
#define ICEBERG_LORA_BW 62.5F
#endif

#ifndef ICEBERG_LORA_SF
#define ICEBERG_LORA_SF 8
#endif

#ifndef ICEBERG_LORA_CR
#define ICEBERG_LORA_CR 5
#endif

#ifndef ICEBERG_LORA_PWR
#define ICEBERG_LORA_PWR 22
#endif

#ifndef ICEBERG_LORA_SYNC
#define ICEBERG_LORA_SYNC 0x1B
#endif

#ifndef ICEBERG_LORA_PREAMBLE
#define ICEBERG_LORA_PREAMBLE 16
#endif

#ifndef ICEBERG_LORA_CURRENT_MA
#define ICEBERG_LORA_CURRENT_MA 140
#endif
