// iPhone link over Bluetooth LE (NimBLE-Arduino 2.x).
//
// 1) Music remote — Apple Media Service (AMS). The iPhone connects to this board
//    (pair once in Settings > Bluetooth > "E-TUBE Tap"). On that same connection the
//    board is a GATT *client* of the iPhone's AMS: it receives title/artist/album/
//    duration/playback state/volume and sends play/pause/next/prev/volume commands.
//    Works with whatever app is playing (Spotify, Apple Music, podcasts...).
//
// 2) Bike data — this board's own GATT *server*:
//    - standard Battery Service (0x180F): bike battery %
//    - "E-TUBE Tap" service with speed, assist, battery, range, trip, heart rate and a
//      readable summary string. View it in nRF Connect (free) on the iPhone.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "iphone.h"
#include "state.h"

// ---- Apple Media Service ----
static const NimBLEUUID AMS_SVC("89D3502B-0F36-433A-8EF4-C502AD55F8DC");
static const NimBLEUUID AMS_REMOTE("9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2");
static const NimBLEUUID AMS_ENTITY_UPDATE("2F7CABCE-808D-411F-9A0C-BB92BA96C102");
enum { ENTITY_PLAYER = 0, ENTITY_QUEUE = 1, ENTITY_TRACK = 2 };

// ---- E-TUBE Tap bike data service (custom 128-bit UUIDs) ----
static const char *BIKE_SVC = "a1e70001-7a4b-4c9e-9a2f-3b8e2d1c0e7a";
static const char *CH_SPEED = "a1e70002-7a4b-4c9e-9a2f-3b8e2d1c0e7a";    // uint16, 0.1 km/h
static const char *CH_ASSIST = "a1e70003-7a4b-4c9e-9a2f-3b8e2d1c0e7a";   // uint8 0 ECO 1 NORMAL 2 HIGH
static const char *CH_BATTERY = "a1e70004-7a4b-4c9e-9a2f-3b8e2d1c0e7a";  // uint8 %
static const char *CH_RANGE = "a1e70005-7a4b-4c9e-9a2f-3b8e2d1c0e7a";    // uint16 km
static const char *CH_TRIP = "a1e70006-7a4b-4c9e-9a2f-3b8e2d1c0e7a";     // uint32 m
static const char *CH_HEART = "a1e70007-7a4b-4c9e-9a2f-3b8e2d1c0e7a";    // uint8 bpm (0 = none)
static const char *CH_SUMMARY = "a1e70008-7a4b-4c9e-9a2f-3b8e2d1c0e7a";  // UTF-8 text

static NimBLEServer *srv;
static NimBLECharacteristic *cSpeed, *cAssist, *cBatt, *cRange, *cTrip, *cHeart, *cSummary, *cStdBatt;
static NimBLERemoteCharacteristic *amsRemote = nullptr;

static volatile bool needAmsSetup = false;
static volatile uint16_t phoneConn = BLE_HS_CONN_HANDLE_NONE;
static volatile int pendingCmd = -1;

// ---------------------------------------------------------------- AMS
static void copyStr(char *dst, size_t n, const uint8_t *src, size_t len) {
  len = min(len, n - 1);
  memcpy(dst, src, len);
  dst[len] = 0;
}

static void onEntityUpdate(NimBLERemoteCharacteristic *, uint8_t *d, size_t len, bool) {
  if (len < 3) return;
  const uint8_t entity = d[0], attr = d[1];
  const uint8_t *v = d + 3;
  const size_t vlen = len - 3;
  char tmp[48];
  if (entity == ENTITY_TRACK) {
    if (attr == 0) copyStr(phone.artist, sizeof phone.artist, v, vlen);
    if (attr == 1) copyStr(phone.album, sizeof phone.album, v, vlen);
    if (attr == 2) copyStr(phone.title, sizeof phone.title, v, vlen);
    if (attr == 3) {
      copyStr(tmp, sizeof tmp, v, vlen);
      phone.durationS = atof(tmp);
    }
  } else if (entity == ENTITY_PLAYER) {
    if (attr == 0) copyStr(phone.player, sizeof phone.player, v, vlen);
    if (attr == 1) {  // "state,rate,elapsed"  state: 0 paused 1 playing 2 rew 3 ff
      copyStr(tmp, sizeof tmp, v, vlen);
      int state = 0;
      float rate = 0, elapsed = 0;
      sscanf(tmp, "%d,%f,%f", &state, &rate, &elapsed);
      phone.playing = state == 1;
      phone.rate = rate;
      phone.elapsedS = elapsed;
      phone.elapsedAtMs = millis();
    }
    if (attr == 2) {  // volume "0.0".."1.0"
      copyStr(tmp, sizeof tmp, v, vlen);
      phone.volumePct = (int)(atof(tmp) * 100 + 0.5f);
    }
  }
}

static void setupAms() {
  NimBLEClient *cl = srv->getClient(phoneConn);
  if (!cl) {
    Serial.println("iPhone: no client for connection");
    return;
  }
  NimBLERemoteService *svc = cl->getService(AMS_SVC);
  if (!svc) {
    Serial.println("iPhone: Apple Media Service not found (not an iPhone, or not paired)");
    return;
  }
  amsRemote = svc->getCharacteristic(AMS_REMOTE);
  NimBLERemoteCharacteristic *eu = svc->getCharacteristic(AMS_ENTITY_UPDATE);
  if (!amsRemote || !eu || !eu->subscribe(true, onEntityUpdate)) {
    Serial.println("iPhone: AMS subscribe failed");
    amsRemote = nullptr;
    return;
  }
  const uint8_t track[] = {ENTITY_TRACK, 0, 1, 2, 3};  // artist, album, title, duration
  const uint8_t player[] = {ENTITY_PLAYER, 0, 1, 2};    // name, playback info, volume
  eu->writeValue(track, sizeof track, true);
  eu->writeValue(player, sizeof player, true);
  phone.ams = true;
  Serial.println("iPhone: music remote ready");
}

// ---------------------------------------------------------------- server callbacks
class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &ci) override {
    Serial.printf("iPhone: connected %s\n", ci.getAddress().toString().c_str());
    phone.connected = true;
    phoneConn = ci.getConnHandle();
    // AMS data is encrypted: start pairing (first time) or encryption (already paired).
    NimBLEDevice::startSecurity(ci.getConnHandle());
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override {
    Serial.printf("iPhone: disconnected (%d)\n", reason);
    phone.connected = phone.paired = phone.ams = phone.playing = false;
    phone.title[0] = phone.artist[0] = phone.album[0] = phone.player[0] = 0;
    phone.volumePct = -1;
    amsRemote = nullptr;
    phoneConn = BLE_HS_CONN_HANDLE_NONE;
  }
  void onAuthenticationComplete(NimBLEConnInfo &ci) override {
    phone.paired = ci.isEncrypted();
    Serial.printf("iPhone: pairing %s\n", phone.paired ? "OK" : "FAILED");
    if (phone.paired) needAmsSetup = true;  // GATT work happens in iphone_loop(), not here
  }
};
static ServerCB serverCB;

// ---------------------------------------------------------------- bike data service
static NimBLECharacteristic *addChar(NimBLEService *s, const char *uuid, const char *desc) {
  NimBLECharacteristic *c = s->createCharacteristic(uuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  NimBLEDescriptor *d = c->createDescriptor("2901", NIMBLE_PROPERTY::READ, 40);  // user description
  d->setValue(desc);
  return c;
}

void iphone_begin() {
  // NimBLEDevice::init() already ran in hr_begin().
  NimBLEDevice::setSecurityAuth(true /*bond*/, false /*no MITM*/, true /*secure conn*/);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);  // "Just Works" pairing

  srv = NimBLEDevice::createServer();
  srv->setCallbacks(&serverCB, false);
  srv->advertiseOnDisconnect(true);

  NimBLEService *bike = srv->createService(BIKE_SVC);
  cSpeed = addChar(bike, CH_SPEED, "Speed (0.1 km/h)");
  cAssist = addChar(bike, CH_ASSIST, "Assist 0 ECO 1 NORMAL 2 HIGH");
  cBatt = addChar(bike, CH_BATTERY, "Battery (%)");
  cRange = addChar(bike, CH_RANGE, "Range (km)");
  cTrip = addChar(bike, CH_TRIP, "Trip (m)");
  cHeart = addChar(bike, CH_HEART, "Heart rate (bpm)");
  cSummary = addChar(bike, CH_SUMMARY, "Summary");
  bike->start();

  NimBLEService *batt = srv->createService("180F");
  cStdBatt = batt->createCharacteristic("2A19", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  batt->start();

  // Advertising: flags + "solicit Apple Media Service" (lets iOS list it in Settings > Bluetooth)
  NimBLEAdvertisementData adv;
  adv.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  const uint8_t solicit[18] = {17, 0x15,  // 128-bit service solicitation, little-endian AMS UUID
                               0xDC, 0xF8, 0x55, 0xAD, 0x02, 0xC5, 0xF4, 0x8E,
                               0x3A, 0x43, 0x36, 0x0F, 0x2B, 0x50, 0xD3, 0x89};
  adv.addData(solicit, sizeof solicit);
  NimBLEAdvertisementData scanResp;
  scanResp.setName("E-TUBE Tap");
  scanResp.addServiceUUID(NimBLEUUID(BIKE_SVC));

  NimBLEAdvertising *a = NimBLEDevice::getAdvertising();
  a->setAdvertisementData(adv);
  a->setScanResponseData(scanResp);
  a->start();
  Serial.println("iPhone: advertising as \"E-TUBE Tap\"");
}

void iphone_cmd(uint8_t amsCommand) { pendingCmd = amsCommand; }

void iphone_loop() {
  if (needAmsSetup) {
    needAmsSetup = false;
    setupAms();
  }
  if (pendingCmd >= 0) {
    uint8_t c = pendingCmd;
    pendingCmd = -1;
    if (amsRemote) amsRemote->writeValue(&c, 1, true);
  }

  // bike data, once a second
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  uint16_t spd = (uint16_t)(ride.speedKph * 10 + 0.5f);
  uint8_t assist = ride.assist, bat = ride.batteryPct, hr = heart.bpm > 255 ? 255 : heart.bpm;
  uint16_t range = ride.rangeKm < 0 ? 0 : ride.rangeKm;  // 0 = assist off
  uint32_t trip = (uint32_t)(ride.tripKm * 1000);
  static const char *names[] = {"ECO", "NORMAL", "HIGH", "OFF"};
  char sum[96];
  snprintf(sum, sizeof sum, "%s%.1f km/h, %s, battery %d%%, range %d km, trip %.1f km%s",
           ride.simulated ? "[SIM] " : "", ride.speedKph, names[ride.assist], ride.batteryPct,
           (int)range, ride.tripKm, heart.bpm ? "" : "");
  cSpeed->setValue(spd);
  cAssist->setValue(assist);
  cBatt->setValue(bat);
  cRange->setValue(range);
  cTrip->setValue(trip);
  cHeart->setValue(hr);
  cSummary->setValue(sum);
  cStdBatt->setValue(bat);
  if (phone.connected) {
    cSpeed->notify();
    cAssist->notify();
    cBatt->notify();
    cRange->notify();
    cTrip->notify();
    cHeart->notify();
    cStdBatt->notify();
  }
}
