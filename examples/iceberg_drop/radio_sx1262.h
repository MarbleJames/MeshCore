#pragma once

#include <stdint.h>
#include <stddef.h>

// Wio-SX1262 for XIAO (header version, not the 30-pin BTB).
// D1 DIO1, D2 RESET, D3 BUSY, D4 NSS, D5 RXEN, D8 SCK, D9 MISO, D10 MOSI.
// DIO2 drives the TX side of the RF switch. DIO3 powers the TCXO only
// while the radio is out of sleep, which is what keeps it at a fraction
// of a microamp between reports.

bool iceberg_radio_begin(bool rx_boosted);
void iceberg_radio_sleep();
int iceberg_radio_send(const uint8_t *data, size_t len);

void iceberg_radio_on_rx(void (*cb)());
bool iceberg_radio_start_rx();
int iceberg_radio_read(uint8_t *data, size_t cap, size_t *got);
float iceberg_radio_rssi();
float iceberg_radio_snr();
