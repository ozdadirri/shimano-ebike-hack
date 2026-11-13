// Live ride data from a Shimano EW-EN100 over BLE (NimBLE-Arduino 2.x). READ-ONLY:
// it subscribes to notifications and never writes a characteristic value.
//
// Protocol notes from captures of this bike (logs in ../ble_explorer/logs/):
//   Service 000018ef-5348-494d-414e-4f5f424c4500, char 00002ac1-…: notifies every ~250 ms,
//   cycling message types by byte 0. Multi-byte values are little-endian.
//     type 0x02: [1] assist 0 OFF, 1 ECO, 2 NORMAL, 3 HIGH
//                [2..3] speed, 0.1 km/h   [5] cadence, rpm
//     type 0x03: [1..4] trip, m   [5..6] second distance counter, m (unknown; NOT the odometer:
//                read 19.8 km on a ~3000 km bike)   [7] battery % (probably; read 100)
//                [9..10] average speed, 0.1 km/h (probably)
//                [11..12] max speed, 0.1 km/h   [13..14] [15..16] [17..18] range km for
//                HIGH / NORMAL / ECO (ECO = 136 km confirmed on the bike display)
//     type 0x01: [6] light, 2 = on, 1 = off
//   Battery: standard Battery Service 0x180F / 0x2A19 (percent).
// Pairing is "Just Works" (security set up in iphone.cpp), no passkey.
#include <Arduino.h>
#include "bike_ble.h"
#include "state.h"
#include "secrets.h"

// Set BIKE_ADDR in secrets.h (e.g. "c4:82:cf:xx:xx:xx", as the serial log prints it) so the
// dashboard ignores other riders' bikes. Without it, it takes the first Shimano unit it sees.
#ifndef BIKE_ADDR
#define BIKE_ADDR ""
#endif

static const NimBLEUUID SHIMANO_SVC("000018ef-5348-494d-414e-4f5f424c4500");
static const NimBLEUUID SHIMANO_DATA("00002ac1-5348-494d-414e-4f5f424c4500");
static const NimBLEUUID BAT_SVC((uint16_t)0x180F);
static const NimBLEUUID BAT_LEVEL((uint16_t)0x2A19);
static const uint32_t STALE_MS = 3000;  // no frame for this long = link lost

static volatile bool g_found = false, g_connected = false;
static NimBLEAddress g_addr;
static NimBLEClient *g_client = nullptr;
static uint32_t g_retryMs = 0;
static volatile uint32_t g_lastMs = 0;
static uint16_t g_range[3] = {0, 0, 0};  // indexed by Assist: ECO, NORMAL, HIGH

static inline uint16_t u16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline uint32_t u32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static void onData(NimBLERemoteCharacteristic *, uint8_t *d, size_t n, bool) {
  if (n == 0) return;
  bool ok = true;
  switch (d[0]) {
    case 0x02:
      if (n < 7) { ok = false; break; }
      // bike 0 OFF, 1 ECO, 2 NORMAL, 3 HIGH -> Assist enum
      ride.assist = d[1] == 1 ? ASSIST_ECO : d[1] == 2 ? ASSIST_NORMAL : d[1] == 3 ? ASSIST_HIGH : ASSIST_OFF;
      ride.speedKph = u16(d + 2) / 10.0f;
      ride.cadenceRpm = d[5];
      break;
    case 0x03:
      if (n < 19) { ok = false; break; }
      ride.tripKm = u32(d + 1) / 1000.0f;
      ride.dist2Km = u16(d + 5) / 1000.0f;
      ride.avgKph = u16(d + 9) / 10.0f;
      ride.maxKph = u16(d + 11) / 10.0f;
      g_range[ASSIST_HIGH] = u16(d + 13);
      g_range[ASSIST_NORMAL] = u16(d + 15);
      g_range[ASSIST_ECO] = u16(d + 17);
      break;
    case 0x01:
      if (n < 7) { ok = false; break; }
      ride.lightOn = d[6] == 2;
      break;
    default:
      break;  // 0x00, 0x04: not decoded yet
  }
  if (!ok) { ride.busErrors++; return; }
  g_lastMs = millis();
}

static void onBattery(NimBLERemoteCharacteristic *, uint8_t *d, size_t n, bool) {
  if (n >= 1 && d[0] <= 100) ride.batteryPct = d[0];
}

class BikeClientCB : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *, int reason) override {
    Serial.printf("Bike: disconnected (reason %d)\n", reason);
    g_connected = false;
    ride.bleConnected = false;
    g_retryMs = millis() + 2000;
  }
};
static BikeClientCB clientCB;

bool bike_onAdvert(const NimBLEAdvertisedDevice *dev) {
  if (g_found || g_connected) return false;
  if (!dev->isAdvertisingService(SHIMANO_SVC)) return false;
  if (BIKE_ADDR[0] && dev->getAddress().toString() != BIKE_ADDR) {
    Serial.printf("Bike: ignoring other Shimano unit %s\n", dev->getAddress().toString().c_str());
    return false;
  }
  g_addr = dev->getAddress();
  g_found = true;
  Serial.printf("Bike: found '%s' (%s) RSSI %d\n", dev->getName().c_str(), g_addr.toString().c_str(),
                dev->getRSSI());
  return true;
}

bool bike_wantScan() { return !g_connected && !g_found; }

static void connectBike() {
  if (!g_client) {
    g_client = NimBLEDevice::createClient();
    g_client->setClientCallbacks(&clientCB, false);
    g_client->setConnectTimeout(5000);
  }
  // NimBLE refuses to connect while a scan is running
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (scan->isScanning()) scan->stop();
  heart.scanning = false;
  Serial.println("Bike: connecting...");
  if (!g_client->connect(g_addr)) {
    Serial.printf("Bike: connect failed (error %d)\n", g_client->getLastError());
    g_retryMs = millis() + 3000;
    return;
  }
  NimBLERemoteService *svc = g_client->getService(SHIMANO_SVC);
  NimBLERemoteCharacteristic *ch = svc ? svc->getCharacteristic(SHIMANO_DATA) : nullptr;
  if (!ch || !ch->subscribe(true, onData)) {
    Serial.println("Bike: no data characteristic; disconnecting");
    g_client->disconnect();
    g_retryMs = millis() + 5000;
    return;
  }
  if (NimBLERemoteService *bs = g_client->getService(BAT_SVC)) {
    if (NimBLERemoteCharacteristic *bc = bs->getCharacteristic(BAT_LEVEL)) {
      NimBLEAttValue v = bc->readValue();
      if (v.length()) onBattery(bc, (uint8_t *)v.data(), v.length(), false);
      if (bc->canNotify()) bc->subscribe(true, onBattery);
    }
  }
  g_connected = true;
  ride.bleConnected = true;
  Serial.println("Bike: live data");
}

void bike_loop() {
  const uint32_t now = millis();
  if (g_found && !g_connected && (int32_t)(now - g_retryMs) >= 0) {
    g_found = false;
    connectBike();
  }
  const bool fresh = g_lastMs && now - g_lastMs < STALE_MS;
  if (fresh) ride.simulated = false;  // first real frame ends the simulator for good
  if (!ride.simulated) {
    ride.busOk = fresh;
    if (!fresh) {
      ride.speedKph = 0;
      ride.cadenceRpm = 0;
    }
    if (ride.assist == ASSIST_OFF) ride.rangeKm = -1;  // no assist: shown as "-- km"
    else if (g_range[ride.assist]) ride.rangeKm = g_range[ride.assist];
  }
}
