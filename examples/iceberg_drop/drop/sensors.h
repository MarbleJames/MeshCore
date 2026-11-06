#pragma once

#include <stdint.h>

// NFC pads on the underside of the XIAO. Solder an SHT40/SHT41 there.
// SDA = NFC1 (pad 30), SCL = NFC2 (pad 31), plus 3V3 and GND.
// A bare sensor, not a breakout with its own regulator and LED.

bool sht_present();
bool sht_begin();
bool sht_read(int16_t *temp_cc, uint16_t *rh_cp);

bool imu_present();
bool imu_begin();
void imu_power(bool on);
void imu_clear();
bool imu_motion_pending();
uint32_t imu_int_pin();
