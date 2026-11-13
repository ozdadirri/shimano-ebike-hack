# EW-EN100 Bluetooth LE: protocol notes

Live ride data from a Shimano STEPS bike, read over Bluetooth LE from the **EW-EN100**
wireless unit by an ESP32-S3. No wires to the bike, no E-TUBE bus tap, and nothing is
ever written to the bike.

This is the route that works today. The wired E-TUBE tap in the [build guide](https://ozdadirri.github.io/shimano-ebike-hack/)
is still a design.

> Reverse-engineered from captures of one bike. Not from Shimano, not affiliated with or
> endorsed by Shimano. Field meanings are marked **confirmed** (checked against the bike)
> or **likely** (fits the data, not yet checked). Other units or firmware may differ.

## The test bike

| | |
|---|---|
| Bike | **Borghini**, a blue Merida eSpresso 400 S EQ (named after Lamborghini: blue is 蓝 *lán*, *lam* in Cantonese) |
| Drive | Shimano STEPS DU-E6100 |
| Wireless unit | EW-EN100, firmware **4.6.0.0**, Bluetooth name = the bike name set in E-TUBE (`Borghini`) |
| Receiver | LCDwiki ES3C28P (ESP32-S3, 2.8" touchscreen), NimBLE-Arduino 2.5.2, Arduino-ESP32 3.3.x |
| Firmware | [`firmware/ble_explorer`](../firmware/ble_explorer) (discovery), [`firmware/etube_ui`](../firmware/etube_ui) (dashboard, `bike_ble.cpp`) |
| Raw captures | [`captures/en100/`](../captures/en100) |

## 1. Finding the EN100

The EN100 advertises under the bike's name with two Shimano 128-bit services. All Shimano
UUIDs end in the ASCII bytes `SHIMANO_BLE` (`53 48 49 4D 41 4E 4F 5F 42 4C 45`):

```
name   'Borghini'
svc    000018ff-5348-494d-414e-4f5f424c4500   (control / E-TUBE settings)
svc    000018ef-5348-494d-414e-4f5f424c4500   (live ride data)
mfr    4A 04 01 00
```

Match on service `000018ef-…`, not the name: the name is whatever the owner set in E-TUBE.
To stay off other riders' bikes, also match the address (`BIKE_ADDR` in `secrets.h`).

**When it advertises.** This tripped us up for a while:

- It advertises **after the bike's system is switched on**, and **only while nothing is
  connected to it**. As soon as a phone running **Shimano Ride** (or E-TUBE Project) connects,
  it goes quiet, and a scanner sees nothing at all.
- Shimano Ride reconnects **in the background**, so having the app closed on screen isn't
  enough. Force-quit it, or turn Bluetooth off, while testing.
- **The trick for finding it the first time:** scan with the bike off, then scan with it on,
  and diff the two lists. Our EN100 was the only new device. The RSSI was about -70 dBm
  from a metre or two away.

## 2. Connecting

- Plain connect, then **Just Works** pairing, with no passkey. NimBLE reported
  `encrypted=1 authenticated=0 bonded=1`. Use security `bond, no MITM, secure connections`
  with IO capability `NoInputNoOutput`.
- **No Shimano login or handshake is needed** for the ride data. Subscribing to `2ac1`
  starts the stream straight away.
- **One central at a time.** If the ESP32 holds the link, Shimano Ride can't connect, and the
  other way round. Phone and dashboard can't both be connected to the EN100.
- After a power-off, the link drops with reason **520** (`0x208`, supervision timeout). When the
  bike is switched on again it advertises again, unless the phone grabs it first.
- NimBLE won't connect while a scan is running: **stop the scan first**. The first version
  of the dashboard missed this and only connected when the scan happened to have ended.

## 3. GATT table (firmware 4.6.0.0)

Properties: R read, W write, w write without response, N notify, I indicate.

| Service | Characteristic | Handle | Props | Read value / use |
|---|---|---:|---|---|
| `1800` Generic Access | `2a00` Device name | 3 | R | `Borghini` |
| | `2a01` Appearance | 5 | R | `80 04` (0x0480, cycling) |
| | `2a04` Conn. parameters | 7 | R | `10 00 20 00 00 00 58 02` (20–40 ms interval, 6 s timeout) |
| `1801` Generic Attribute | `2a05` Service changed | 10 | I | |
| `180a` Device Information | `2a29` Manufacturer | 14 | R | `SHIMANO` |
| | `2a26` Firmware | 16 | R | `4.6.0.0` |
| `18ff-…SHIMANO_BLE` control | `2af3` | 19 | W I | subscribing triggers pairing |
| | `2af4` | 22 | R | 16 device-specific bytes (redacted in the captures) |
| | `2af5` | 24 | W | |
| | `2af6` | 26 | R | empty before authentication |
| | `2af7`, `2af8` | 28, 30 | R W | empty before authentication |
| | `2af9`, `2afb`, `2afd` | 32, 37, 42 | N | quiet |
| | `2afa`, `2afc` | 35, 40 | W w | |
| | `2afe`, `2aff` | 45, 47 | W | |
| `18fe-1212-efde-1523-785feabcd123` | `2ae2` | 50 | R | `65 00 13 01 00 00` |
| | `2ae3` | 52 | R | the unit's own Bluetooth address, byte-reversed |
| `180f` Battery | `2a19` Battery level | 55 | R N | `64` = **100 %** |
| `18ef-…SHIMANO_BLE` ride data | `2ac0` | 59 | R I | `10 00 00 00 00 01 02` |
| | **`2ac1` live data** | **62** | **N** | **the ride data stream, see below** |
| | `2ac2` | 65 | I | quiet |
| | `2ac3` | 68 | R I | `00 00 FF 00 FF 00 …` (20 bytes) |
| | `2ac4` | 71 | W I | |

The `18ff` control service is what E-TUBE uses for settings and firmware. Our code never writes to
it, and **writing to unknown characteristics on a motor controller is a bad idea**. The 128-bit
service `18fe-1212-efde-1523-785feabcd123` uses a Nordic-style base UUID. It may belong to the radio
module's own firmware.

## 4. The ride data contract: `18ef` / `2ac1`

`2ac1` sends a notification about every **250 ms**. Byte 0 is the **message type**, and the
types take turns, so each one repeats about once a second. Multi-byte numbers are
**little-endian**. Byte positions count from 0, where byte 0 is the type.

### Type `0x02`: live riding values (about 1 Hz)

| Byte | Field | Scale | Status |
|---:|---|---|---|
| 1 | **Assist mode** | 0 OFF, 1 ECO, 2 NORMAL, 3 HIGH (E6100 names; E8000 calls them ECO/TRAIL/BOOST) | **confirmed** (stepped 0→1→2→3 with the switch) |
| 2–3 | **Speed** | 0.1 km/h | **confirmed** (0 standing, 28.9 km/h peak with the wheel spun) |
| 4 | ? | 0–3 while pedalling | unknown, maybe motor or torque level |
| 5 | **Cadence** | rpm | **likely** (53 → 103 while pedalling faster, 0 when stopped) |
| 6 | ? flag | 0/1 | unknown. Went to 1 at ~26 km/h and stayed there |
| 7–9 | | always 0 | |

### Type `0x03`: distances, speeds and range (about 1 Hz)

| Byte | Field | Scale | Status |
|---:|---|---|---|
| 1–4 | **Trip distance** | m | **likely** (+115 m during a ~16 s spin at ~25 km/h, which fits). Not yet compared with the display |
| 5–6 | Second distance counter | m | unknown. Went up exactly like the trip, but read 19.8 km. **Not the odometer** (the bike has done about 3000 km) |
| 7 | Battery? | % | **likely** (reads 100, the same as the Battery service) |
| 8 | | 0 | |
| 9–10 | Average speed | 0.1 km/h | **likely** (13.2 → 15.7 km/h, only goes up during the test) |
| 11–12 | **Maximum speed** | 0.1 km/h | **likely** (22.5 → 29.1, just above the 28.9 peak we saw) |
| 13–14 | **Range in HIGH** | km | **likely** (68) |
| 15–16 | **Range in NORMAL** | km | **likely** (95) |
| 17–18 | **Range in ECO** | km | **confirmed** (136, the same as the bike display in ECO) |

With assist OFF the range has no meaning. The dashboard shows `-- km`.

### Type `0x01`: status (occasional)

| Byte | Field | Status |
|---:|---|---|
| 6 | **Light**: 2 on, 1 off | **confirmed** (toggled the light on, then off) |
| 9–10 | `C2 11` | unknown, constant |
| 14 | 0/1 | unknown. Dropped to 0 just before pedalling started, then back to 1 |

### Type `0x00` and `0x04`

`0x00` stayed constant: `00 00 FF FF FF FF FF 80 80 80 80 80 80 FF FF FF FF`. The `FF`/`80`
pattern looks like "not fitted" values, maybe for Di2 gears this bike doesn't have.
`0x04 FF FF` appears rarely. Neither is decoded.

### Battery

Use the **standard Battery service** (`180f` / `2a19`): read it once, then subscribe.

### Example frames

```
02 00 00 00 00 00 00 00 00 00                            standing, assist OFF
02 03 21 01 00 00 01 00 00 00                            HIGH, 28.9 km/h
03 D9 00 00 00 64 4D 64 00 84 00 E1 00 45 00 61 00 8B 00 trip 217 m, ranges 69/97/139 km
01 FF FF FF 00 00 02 00 00 C2 11 FF FF FF 01             light on
```

## 5. Not decoded yet (next experiments)

| What | How to find it |
|---|---|
| **Odometer** (about 3000 km) | Note the odometer and trip in Shimano Ride, then capture. Search every message for 3000 km in m, 0.1 km or km. It may only be readable through `18ff` after E-TUBE authentication, which we won't write to |
| Walk assist | Hold walk mode for 5 s during a capture |
| Error codes | Note any code the display shows. Never cause faults on purpose |
| Type `02` byte 4 and 6, type `01` byte 14 | Ride outdoors with the capture running and compare with what the bike was doing |
| Range order HIGH/NORMAL | Read the range on the display in HIGH and NORMAL |

Capture with [`tools/en100_capture.py`](../tools/en100_capture.py) and the `ble_explorer` sketch.
It waits for the EN100, connects, and logs every notification with a timestamp.

## 6. Phone and dashboard together

The ESP32 runs several Bluetooth links at once (NimBLE allows 3 connections by default):

```
            central (ESP32 connects out)          peripheral (iPhone connects in)
 EW-EN100 ──────────────► ESP32-S3 ◄──────────────── iPhone
 HR sender ─────────────►  "E-TUBE Tap"             • Apple Media Service: music remote
 (HeartCast / strap)                                • bike data GATT service a1e70001-…
```

1. **iPhone:** Settings → Bluetooth → **E-TUBE Tap** → pair (Just Works). The music remote and the bike
   data service then work.
2. **Bike:** the dashboard scans for the EN100 by itself. **Keep Shimano Ride force-quit while riding with
   the dashboard.** Otherwise the phone takes the EN100 first and the dashboard shows NO BUS.
3. If you need Shimano Ride (odometer, settings, firmware), open it. The ESP32 loses the bike while Ride is
   connected. Force-quit Ride and switch the bike off and on, and the dashboard gets it back.

The iPhone link to the ESP32 and Shimano Ride's link to the EN100 are separate. Only the EN100 itself is
limited to one connection.

## 7. Debugging lessons

| Symptom | Cause / fix |
|---|---|
| Serial monitor blank after upload | The sketch printed its scan before the monitor opened, then waited for input. Reset after opening the monitor, or type a command (`s`). |
| Upload fails: `No serial data received`, port still listed | The board's native USB serial locked up after many fast resets. **Unplug and replug the board.** |
| Can't take the port | Arduino IDE's Serial Monitor holds it. Close the monitor tab first. |
| EN100 missing from scans | Something is connected to it (Shimano Ride in the background), or the bike is off. Diff a bike-off and a bike-on scan. |
| Connected once, never again | Connecting while scanning. Stop the scan before `connect()`. |
| `invalid conversion from 'int' to 'lv_part_t'` | LVGL 9.6 is stricter than 9.2. Use `LV_PART_MAIN` instead of `0`. |
| A "6573 km odometer" | First guess was type `03` bytes 5–8 as one 32-bit number. Wrong: byte 7 is battery %, and bytes 5–6 read 19.8 km on a 3000 km bike. Check a guess against the bike before you trust it. |

## 8. Safety

- The code only **subscribes** (it writes the standard CCCD descriptor to enable notifications) and **reads**.
  It never writes a characteristic value, never touches the `18ff` control service, and can't change assist,
  the 25 km/h limit, settings or firmware.
- Treat the dashboard as an extra display. If the link drops, the bike rides exactly as normal.
