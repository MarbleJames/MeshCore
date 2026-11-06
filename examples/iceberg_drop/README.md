# Iceberg drop node

A XIAO nRF52840 Sense, a Wio-SX1262 and a XIAO L76K, dropped on an iceberg. It sleeps, and about once an hour it takes a GPS fix, reads temperature and humidity, sends one short packet, and goes back to sleep. If the onboard IMU sees a large roll or a sharp jolt, it reports about every 10 seconds until the ice has been still for a minute. A second board, plugged into a PC, stays listening and a small page graphs what arrives.

This is not a MeshCore repeater or sensor node. Those stay in receive. Receive is what empties the cell.

## Why the radio sleeps

MeshCore's companion, repeater and sensor firmware keep the SX1262 in continuous receive. That is the right shape for a mesh: someone may transmit at any moment. On this radio it costs about 5 mA before the MCU is counted, which is roughly 120 mAh a day. A cold 18650 then lasts days, not months, and that is before the GPS is turned on.

The nRF52 sleep helper in MeshCore does not change that. `NRF52Board::sleep()` ignores the requested time and waits for the next interrupt. `powerOff()` is system-off, used when the cell is already empty. System-off on the nRF52840 cannot wake from its own RTC. Timed wake from system-off needs an external RTC or a pin. MeshCore's ESP32 boards do have a real deep-sleep timer. The nRF52 builds do not, because the mesh is supposed to be listening.

Meshtastic's tracker role, with power saving turned on, is the closer model. The cycle is: wake, get a fix, send the position, sleep for `position_broadcast_secs`, and the radio is included in that sleep. On nRF52 they stay in system-on low power rather than system-off, for the same RTC reason. A measured RAK in that mode sat around 0.8–0.9 mA once the obvious peripherals were off, with the GPS and regulators still the large terms.

This firmware follows that cycle and then goes further. The drop node never opens a receive window. Nothing is going to talk to it until a drone is overhead on a schedule, and an open receiver is the expensive part. The gateway on the desk is the one that listens, because it has USB power.

## What actually uses the current

Budget these from the module datasheets, then measure a built unit. The GPS term swamps the rest.

| While asleep between hourly reports | Current |
| --- | --- |
| SX1262 warm sleep, TCXO off via DIO3 | under 1 µA |
| nRF52840 system-on, DC-DC, RTC tick | about 20 µA |
| Battery divider left on (see below) | about 3 µA |
| QSPI flash in deep power-down | about 1 µA |
| LSM6DS3TR-C accelerometer at 12.5 Hz | about 15 µA |
| SHT40 idle | under 1 µA |
| XIAO L76K held in standby | 360 µA (Seeed, with the active antenna) |

The L76K chip itself is about 20 µA in standby. Seeed's module figure of 360 µA includes the antenna bias, and that is the number to plan with until you measure the stack. Standby is still the right choice. A cold start every hour (GPS fully unpowered, 60–90 s at about 41 mA) costs more energy than leaving the module in standby and taking a hot fix.

The battery divider stays connected. Switching it off floats the ADC pin up to the raw cell voltage, which can exceed what the nRF pin allows. The resistors draw about 3 µA. A comment in the shared XIAO variant guesses milliamps; that is the ratio written as if it were the resistor values.

| Once an hour | Energy |
| --- | --- |
| Hot fix, about 15 s at 41 mA | about 4 mAh/day |
| One 22 dBm packet | under 1 mAh/day |
| Sleep at about 0.4 mA | about 10 mAh/day |
| **Calm day** | **about 15 mAh** |

A 3000 mAh 18650 at room temperature is then on the order of half a year. At iceberg temperatures, plan on about half the nameplate capacity, so roughly 100 days from one cell and 200 days from two cells in parallel. A roll leaves the GPS tracking at about 41 mA until the IMU has been quiet for a minute. An hour of that is about 41 mAh. That is the "keep reporting until it stops, or the cell is empty" path, and it is deliberate.

When the cell is under 3.2 V the node sends one last packet and enters system-off. It does not keep transmitting into a brown-out. Reset, or plugging into USB, brings it back. Charging a lithium cell below 0 °C is a separate problem: this firmware does not charge, and the XIAO's onboard charger should not be asked to.

## Sensors

Use the **XIAO nRF52840 Sense**. The LSM6DS3TR-C is already on the board, on its own I2C bus, and it can wake the MCU from a latched interrupt while drawing about 15 µA. A plain XIAO nRF52840 still does the hourly report. It just cannot see a roll.

The fast reports start on either of:

- a wake-up of about 0.25 g that lasts two samples (a calving jolt, not a tick)
- the orientation interrupt at 50 degrees (the most sensitive step this chip has)

A slow tilt smaller than that waits for the next hourly fix. The threshold is `ICEBERG_WAKE_THS` in `config.h`.

For air temperature and humidity, solder a **Sensirion SHT41** (SHT40 is the same idea) to the NFC pads on the underside of the XIAO:

- SDA to NFC1
- SCL to NFC2
- 3V3 and GND
- 4.7 kΩ from SDA to 3V3 and from SCL to 3V3 if the part has no pull-ups

Use the bare sensor. A breakout with its own regulator and LED can sit at hundreds of microamps and erase the point of choosing this part. The SHT4x idles under a microamp, measures in one shot with the heater left off, and works to −40 °C.

Do not use a BME680, SGP40 or SCD4x here. Those run a heater at milliamps. A BME280 is acceptable and worse: a few microamps asleep, and many boards never really sleep because of their regulator. The nRF die temperature is sent if no SHT is fitted, and the packet says so.

There is no header pin left for a normal I2C sensor. The Wio-SX1262 takes D1–D5 and SPI on D8–D10. The L76K takes D0 (wake), D6 and D7 (UART). MeshCore's usual XIAO build moves I2C onto D6/D7, which is why that build cannot also carry this GPS without a rewire.

## Battery

One or two protected 18650 cells, **in parallel only**. Series is 7.4 V and will kill the XIAO. Wire them to the BAT pads on the back. The onboard BQ25100 is a 50 mA charger for a small pouch cell. Charge the 18650s in a proper charger, and do not charge them in the cold. Keep the cells a few centimetres away from the LoRa antenna.

## Packet

34 bytes, little endian, CRC-16/CCITT-FALSE. Radio settings match the European MeshCore band so a later drone receiver can sit on the same channel, with a different sync word (`0x1B`, not MeshCore's `0x12`) so the two protocols do not parse each other.

`869.618 MHz`, bandwidth `62.5 kHz`, spreading factor `8`, coding rate `4/5`, `22 dBm`. All of that is in `config.h`.

On USB the drop node also prints one JSON line per report and shortens the calm interval to 60 seconds, so a desk test does not wait an hour. On battery the interval is one hour and the USB console is shut down.

## Build

Drop node:

```bash
pio run -e Xiao_nrf52_iceberg_drop -t create_uf2
```

Gateway (the board that stays on the PC):

```bash
pio run -e Xiao_nrf52_iceberg_gateway -t create_uf2
```

Double-tap reset on the XIAO so the drive appears, and copy the `.uf2` onto it. Stack is Sense, then Wio-SX1262, then L76K, with the GNSS antenna in the clear. The gateway does not need the GPS or the humidity sensor.

## Page

```bash
pip install -r examples/iceberg_drop/ui/requirements.txt
python3 examples/iceberg_drop/ui/monitor.py --port /dev/ttyACM0
```

Open the URL it prints. The page shows the latest position, metres from the first fix and from the previous report, and plots temperature, humidity and height. Point it at the gateway. The drop node's own USB JSON is marked `src: usb` and can be watched the same way before the radio link is up.

The codec and the NMEA parser have a host test:

```bash
g++ -std=c++17 -I examples/iceberg_drop \
  examples/iceberg_drop/test_host.cpp \
  examples/iceberg_drop/packet.cpp \
  examples/iceberg_drop/nmea.cpp \
  -o /tmp/iceberg_test && /tmp/iceberg_test
```

## Later, the drone

The hourly transmitter is the first listener's problem solved. A drone that is only overhead at set times should not require this node to listen all day. When that schedule exists, the change is a short receive window at those times, after the report, not a mesh stack left in receive.
