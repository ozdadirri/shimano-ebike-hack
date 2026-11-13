// BLE heart-rate receiver (NimBLE-Arduino 2.x).
// Scans for anything advertising the standard Heart Rate Service (0x180D),
// connects, and subscribes to Heart Rate Measurement (0x2A37) notifications.
// Works with chest straps, watches that "broadcast heart rate", and
// HeartCast (Apple Watch → iPhone → BLE). Keep HeartCast open on the iPhone.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "hr_ble.h"
#include "state.h"
#include "bike_ble.h"

static const NimBLEUUID HRS_UUID((uint16_t)0x180D);
static const NimBLEUUID HRM_UUID((uint16_t)0x2A37);

static volatile bool g_found = false;
static NimBLEAddress g_addr;
static char g_foundName[24];
static NimBLEClient *g_client = nullptr;
static uint32_t g_nextScanMs = 0;

static void onHrNotify(NimBLERemoteCharacteristic *, uint8_t *d, size_t len, bool) {
  if (len < 2) return;
  // Flags bit0: 0 = uint8 bpm, 1 = uint16 bpm
  int bpm = (d[0] & 0x01) ? (len >= 3 ? (d[1] | (d[2] << 8)) : 0) : d[1];
  heart.bpm = bpm;
  heart.lastMs = millis();
}

class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    // one scan serves both links: the EN100 (bike_ble.cpp) and the heart-rate sender
    if (bike_onAdvert(dev) && (heart.connected || g_found)) {
      NimBLEDevice::getScan()->stop();
      return;
    }
    if (g_found || heart.connected) return;
    const std::string nm = dev->getName();
    bool hr = dev->isAdvertisingService(HRS_UUID) ||
              nm.find("HeartCast") != std::string::npos;
    if (!hr) return;
    g_addr = dev->getAddress();
    strlcpy(g_foundName, nm.empty() ? "HR sensor" : nm.c_str(), sizeof g_foundName);
    g_found = true;
    Serial.printf("HR: found %s (%s) RSSI %d\n", g_foundName, g_addr.toString().c_str(),
                  dev->getRSSI());
    if (!bike_wantScan()) NimBLEDevice::getScan()->stop();
  }
  void onScanEnd(const NimBLEScanResults &, int) override { heart.scanning = false; }
};
static ScanCB scanCB;

class ClientCB : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *, int reason) override {
    Serial.printf("HR: disconnected (reason %d)\n", reason);
    heart.connected = false;
    heart.bpm = 0;
    g_nextScanMs = millis() + 2000;
  }
};
static ClientCB clientCB;

static void startScan() {
  g_found = false;
  heart.scanning = true;
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCB, false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(80);
  scan->start(15000, false, true);  // 15 s, then hr_loop restarts it
}

static void connectFound() {
  if (!g_client) {
    g_client = NimBLEDevice::createClient();
    g_client->setClientCallbacks(&clientCB, false);
    g_client->setConnectTimeout(5000);
  }
  Serial.printf("HR: connecting to %s...\n", g_foundName);
  if (!g_client->connect(g_addr)) {
    Serial.println("HR: connect failed");
    g_nextScanMs = millis() + 3000;
    return;
  }
  NimBLERemoteService *svc = g_client->getService(HRS_UUID);
  NimBLERemoteCharacteristic *ch = svc ? svc->getCharacteristic(HRM_UUID) : nullptr;
  if (!ch || !ch->canNotify() || !ch->subscribe(true, onHrNotify)) {
    Serial.println("HR: no heart-rate characteristic; disconnecting");
    g_client->disconnect();
    g_nextScanMs = millis() + 3000;
    return;
  }
  strlcpy(heart.name, g_foundName, sizeof heart.name);
  heart.connected = true;
  Serial.printf("HR: subscribed to %s\n", heart.name);
}

void hr_begin() {
  NimBLEDevice::init("E-TUBE Tap");
  g_nextScanMs = millis() + 1000;
}

void hr_loop() {
  const uint32_t now = millis();
  if (heart.connected && heart.bpm && now - heart.lastMs > 5000) heart.bpm = 0;  // stale reading
  if (g_found && !heart.connected) {
    if (heart.scanning) NimBLEDevice::getScan()->stop();  // can't connect while scanning
    g_found = false;
    connectFound();
    return;
  }
  bike_loop();
  // keep scanning until both the bike and a heart-rate sender are connected
  const bool need = !heart.connected || bike_wantScan();
  if (need && !heart.scanning && (int32_t)(now - g_nextScanMs) >= 0) startScan();
}
