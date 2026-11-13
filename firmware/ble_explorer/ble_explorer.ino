/* EN100 GATT explorer — connect, list services/characteristics, read what is
   readable, subscribe to every notify/indicate, and hex-dump incoming data.
   Purpose: find which characteristic carries speed / assist / battery.
   Never writes characteristic values (only the standard CCCD to subscribe).

   Libraries: NimBLE-Arduino 2.x. Board: any ESP32-S3 (serial output only).
   Serial 115200. Commands:
     <n>        connect to device number n from the scan list
     s          rescan
     k<passkey> set passkey for pairing (default 000000), e.g. k123456
     d          disconnect
*/
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>

static uint32_t passkey = 0;
static std::vector<NimBLEAddress> found;
static std::vector<std::string> foundNames;
static NimBLEClient *client = nullptr;
static int autoPick = -1;

static void hexdump(const uint8_t *d, size_t n) {
  for (size_t i = 0; i < n; i++) Serial.printf("%02X ", d[i]);
  Serial.print(" |");
  for (size_t i = 0; i < n; i++) Serial.print(isprint(d[i]) ? (char)d[i] : '.');
  Serial.println("|");
}

static bool looksShimano(const NimBLEAdvertisedDevice *dev) {
  std::string n = dev->getName();
  for (auto &c : n) c = toupper(c);
  if (n.find("SHIMANO") != std::string::npos || n.find("EN100") != std::string::npos ||
      n.find("STEPS") != std::string::npos || n.find("EP") == 0)
    return true;
  for (int i = 0; i < dev->getServiceUUIDCount(); i++) {
    // Shimano 128-bit UUIDs end in ASCII "SHIMANO_BLE" (5348494d414e4f5f424c45)
    if (dev->getServiceUUID(i).toString().find("5348-494d-414e") != std::string::npos) return true;
  }
  if (dev->haveManufacturerData()) {
    std::string m = dev->getManufacturerData();
    if (m.size() >= 2 && (uint8_t)m[0] == 0x76 && (uint8_t)m[1] == 0x01) return true;  // 0x0176 = Shimano?
  }
  return false;
}

class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    for (auto &a : found) if (a == dev->getAddress()) return;
    int idx = found.size();
    found.push_back(dev->getAddress());
    foundNames.push_back(dev->getName());
    bool shim = looksShimano(dev);
    Serial.printf("[%2d] %s  RSSI %4d  name='%s'%s\n", idx, dev->getAddress().toString().c_str(),
                  dev->getRSSI(), dev->getName().c_str(), shim ? "   <== Shimano?" : "");
    for (int i = 0; i < dev->getServiceUUIDCount(); i++)
      Serial.printf("       svc %s\n", dev->getServiceUUID(i).toString().c_str());
    if (dev->haveManufacturerData()) {
      std::string m = dev->getManufacturerData();
      Serial.print("       mfr ");
      hexdump((const uint8_t *)m.data(), m.size());
    }
    if (shim && autoPick < 0) autoPick = idx;
  }
  void onScanEnd(const NimBLEScanResults &, int) override {
    Serial.println("Scan done. Type a number to connect, or 's' to rescan.");
    if (autoPick >= 0) Serial.printf("Likely EN100: [%d] (type %d)\n", autoPick, autoPick);
  }
} scanCB;

class ClientCB : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *) override { Serial.println("Connected."); }
  void onDisconnect(NimBLEClient *, int reason) override {
    Serial.printf("Disconnected, reason %d (0x%X)\n", reason, reason);
  }
  void onPassKeyEntry(NimBLEConnInfo &info) override {
    Serial.printf("Bike asks for passkey -> sending %06lu\n", (unsigned long)passkey);
    NimBLEDevice::injectPassKey(info, passkey);
  }
  void onConfirmPasskey(NimBLEConnInfo &info, uint32_t pin) override {
    Serial.printf("Confirm passkey %06lu -> yes\n", (unsigned long)pin);
    NimBLEDevice::injectConfirmPasskey(info, true);
  }
  void onAuthenticationComplete(NimBLEConnInfo &info) override {
    Serial.printf("Security: encrypted=%d authenticated=%d bonded=%d\n", info.isEncrypted(),
                  info.isAuthenticated(), info.isBonded());
  }
} clientCB;

static void onNotify(NimBLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool isNotify) {
  Serial.printf("%8lu %s h=%3u %s  ", millis(), isNotify ? "NTF" : "IND", chr->getHandle(),
                chr->getUUID().toString().c_str());
  hexdump(data, len);
}

static void explore(int idx) {
  if (idx < 0 || idx >= (int)found.size()) { Serial.println("No such device."); return; }
  NimBLEDevice::getScan()->stop();
  if (!client) {
    client = NimBLEDevice::createClient();
    client->setClientCallbacks(&clientCB, false);
    client->setConnectTimeout(10 * 1000);
  }
  Serial.printf("Connecting to [%d] %s ...\n", idx, found[idx].toString().c_str());
  if (!client->connect(found[idx])) { Serial.println("Connect failed (is the phone app still connected?)"); return; }

  for (auto *svc : client->getServices(true)) {
    Serial.printf("\nSERVICE %s\n", svc->getUUID().toString().c_str());
    for (auto *chr : svc->getCharacteristics(true)) {
      Serial.printf("  CHAR %s h=%u [%s%s%s%s%s]\n", chr->getUUID().toString().c_str(), chr->getHandle(),
                    chr->canRead() ? "R" : "", chr->canWrite() ? "W" : "",
                    chr->canWriteNoResponse() ? "w" : "", chr->canNotify() ? "N" : "",
                    chr->canIndicate() ? "I" : "");
      if (chr->canRead()) {
        NimBLEAttValue v = chr->readValue();
        Serial.print("       = ");
        if (v.length()) hexdump(v.data(), v.length());
        else Serial.println("(empty or not permitted)");
      }
      if (chr->canNotify() || chr->canIndicate()) {
        bool ok = chr->subscribe(chr->canNotify(), onNotify, true);
        Serial.printf("       subscribe: %s\n", ok ? "ok" : "FAILED");
      }
    }
  }
  Serial.println("\n--- Listening. Change assist mode, spin the rear wheel, etc. ---");
}

static void startScan() {
  found.clear();
  foundNames.clear();
  autoPick = -1;
  Serial.println("Scanning 10 s ...");
  NimBLEDevice::getScan()->start(10 * 1000, false, true);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("EN100 explorer starting");
  NimBLEDevice::init("EN100-Explorer");
  Serial.println("BLE init ok");
  NimBLEDevice::setSecurityAuth(true, true, true);           // bond, MITM, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_DISPLAY);  // allows passkey entry
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCB, false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  startScan();
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "s") startScan();
    else if (cmd == "d") { if (client) client->disconnect(); }
    else if (cmd.startsWith("k")) { passkey = cmd.substring(1).toInt(); Serial.printf("Passkey = %06lu\n", (unsigned long)passkey); }
    else if (cmd.length() && isdigit(cmd[0])) explore(cmd.toInt());
  }
  delay(10);
}
