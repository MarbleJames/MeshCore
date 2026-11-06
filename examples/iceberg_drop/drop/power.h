#pragma once

#include <stdint.h>

bool usb_present();
void early_power_setup();
void console_attach();
void prepare_sleep();

// Returns true if the IMU interrupt fired before the deadline.
bool sleep_for_ms(uint32_t ms);

uint16_t read_batt_mv();
int16_t read_die_temp_cc();
void system_off();
