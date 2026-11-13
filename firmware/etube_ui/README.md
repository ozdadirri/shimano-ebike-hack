# E-TUBE Tap — touchscreen bike computer

Firmware for the LCDwiki **ES3C28P** (ESP32-S3, 2.8" 240×320 IPS touchscreen, speaker, microSD).
It shows ride data, a GPS breadcrumb map and heart rate. It also works as a media remote for the iPhone and plays internet radio and MP3 files.

Ride data comes **live from a Shimano EW-EN100 over Bluetooth LE** (`bike_ble.cpp`, read-only): speed, assist mode, battery, range, trip and lights. Until the bike connects, the screen shows simulated values with a **SIM DATA** badge. The track is simulated until a GPS module sends NMEA.
The EN100 protocol notes are in [`docs/en100-ble-protocol.md`](../../docs/en100-ble-protocol.md) in the repo.

## Screens

Use the tab bar at the bottom to change screens, or swipe left/right.

| Screen | What it shows |
|---|---|
| RIDE | Speed, assist mode, battery, range, trip, lights, heart rate, mini player |
| MAP | Breadcrumb track, auto-fitted, with scale bar |
| MUSIC | iPhone now-playing and controls (Apple Media Service) |
| RADIO | Built-in internet stations over Wi-Fi; volume 0–21, then BOOST up to +12 dB |
| MP3 | Every `.mp3` on the SD card, with shuffle and repeat |

Screenshots can be pulled straight from the frame buffer with `tools/screenshot.py` (see below).

The status bar at the top shows the clock (NTP, Sydney time), Wi-Fi, Bluetooth and heart-rate status.

## Features

- **Ride**
  - Speed, assist mode (OFF / ECO / NORMAL / HIGH), battery, range for the current mode, trip and lights, live from the EW-EN100.
  - Status bar: **BUS OK** (live), **NO BUS** (link lost; speed shows 0, never simulated values), or **SIM DATA** (bike not connected yet since boot).
  - The ECO / NORMAL / HIGH buttons only follow the bike when live; the link is read-only, so change mode on the bike's switch.
  - Range shows `-- km` with assist OFF.
  - Heart rate from an Apple Watch running **HeartCast**, using the BLE Heart Rate service (0x180D).
- **Map**
  - Draws a breadcrumb track: up to 1,500 points, one every 5 m.
  - Reads a GPS module on RX14/TX21 when one is fitted; until then the route is simulated.
- **iPhone**
  - Pairs as "E-TUBE Tap".
  - Shows the current track and controls play, skip and volume through Apple Media Service.
  - Shares bike data with the phone through a custom GATT service (`a1e70001-…`) and the standard Battery service.
- **Radio** (9 stations):
  - Hong Kong: 叱咤903, LOVE 972
  - Beijing: 北京音乐广播
  - Australia: Sky News Australia, ABC News Radio, Smooth FM 95.3, ABC Jazz, ABC Classic
  - UK: BBC Radio 1
  - Stations are listed in `state.h` (`STATIONS[]`).
- **MP3**
  - Plays from microSD, searching up to 3 folders deep for at most 400 files, sorted by path.
  - The track advances automatically at the end of each song.
- **Sounds**
  - Button clicks play through the ES8311 codec and speaker.

## Build

| | |
|---|---|
| Core | Arduino-ESP32 3.3.x |
| Board | ESP32S3 Dev Module, USB CDC On Boot **Enabled**, Flash 16 MB, PSRAM **OPI**, Partition Scheme **Custom** (uses `partitions.csv`, 7 MB app) |
| Libraries | LVGL 9.2.2, GFX Library for Arduino 1.6.9, NimBLE-Arduino 2.5.2, TinyGPSPlus 1.0.3, ESP32-audioI2S 3.4.7 |
| `lv_conf.h` | `~/Documents/Arduino/libraries/lv_conf.h`: 512 KB pool in PSRAM, Montserrat 20/28 |

```sh
arduino-cli compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=custom,PSRAM=opi .
arduino-cli upload  --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=custom,PSRAM=opi -p /dev/cu.usbmodem21201 .
```

Press **RST** after uploading. Copy `secrets.example.h` to `secrets.h` and fill in `WIFI_SSID`, `WIFI_PASS` and, optionally, `BIKE_ADDR` (your EN100's address as the serial log prints it, so the dashboard ignores other riders' bikes). `secrets.h` is git-ignored.

**Library versions:** it was written against LVGL 9.2.2 and also builds with LVGL 9.6.0 and ESP32-audioI2S 4.0.1.

## Serial commands (115200)

| Command | Action |
|---|---|
| `r<n>` | play radio station *n* |
| `s` | stop radio / MP3 |
| `t<x>,<y>` | fake a tap at x,y |
| `p` | send a screenshot (raw RGB565) |
| `c` | print the clock |
| `w` | SD card write test |

## Screenshots

The images above come straight from the board's frame buffer:

```sh
python3 -m pip install pyserial
python3 ../../tools/screenshot.py /dev/cu.usbmodem21201 docs/screenshots \
  ride@24,300 map@72,300 music@120,300 radio@168,300 'radio-playing!r6~6' mp3@216,300 '!s'
```

Each argument has the form `name[@x,y][!cmd][~secs]`. The script taps the given point, sends the command, waits, then saves `name.png`.

## Hardware notes (ES3C28P)

| Part | Pins |
|---|---|
| LCD ILI9341V (SPI) | CS 10, DC 46, SCK 12, MOSI 11, MISO 13, BL 45 (colour inversion on) |
| Touch FT6336 (I2C 0x38) | SDA 16, SCL 15, INT 17, RST 18 |
| Audio ES8311 (I2C 0x18) | MCLK 4, BCLK 5, WS 7, DOUT 8; amp enable GPIO 1 (LOW = on) |
| microSD (SD_MMC) | CLK 38, CMD 40, D0 39, D1 41, D2 48, D3 47 |
| GPS (planned) | RX 14, TX 21 |

## Source layout

| File | Purpose |
|---|---|
| `etube_ui.ino` | display/touch setup, LVGL glue, simulator (until the bike connects), serial commands |
| `ui.cpp` | all five screens and the tab bar |
| `state.h` | shared state structs and the station list |
| `radio.cpp` | audio player task (radio + MP3), SD scan |
| `sound.cpp` | ES8311 codec init, click sounds, volume boost |
| `iphone.cpp` | Apple Media Service client + bike-data GATT server |
| `hr_ble.cpp` | shared BLE scan; heart-rate central (HeartCast) |
| `bike_ble.cpp` | EW-EN100 central: connect, decode ride data into `RideState` |
| `gps_track.cpp` | GPS parsing and breadcrumb track |
| `font_*.c` | generated fonts (Saira Condensed, JetBrains Mono, Noto Sans SC) |
