// GPS input (u-blox style NMEA over UART) + breadcrumb track recorder.
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include "gps_track.h"
#include "state.h"

constexpr int PIN_GPS_RX = 14;  // ← GPS module TX
constexpr int PIN_GPS_TX = 21;  // → GPS module RX (unused, but wired for config)
constexpr uint32_t GPS_BAUD = 9600;

constexpr int MAX_POINTS = 1500;
constexpr float MIN_STEP_M = 5.0f;  // record a point every ≥5 m

static TinyGPSPlus nmea;
static float tx[MAX_POINTS], ty[MAX_POINTS];
static int tn = 0;
static bool haveOrigin = false;
static double lat0, lon0, cosLat0;
static float curX = 0, curY = 0;
static uint32_t lastNmeaMs = 0;

// Simulated ride, used until real NMEA shows up.
static double simLat = -27.4705, simLon = 153.0260;  // arbitrary start point
static float simHeading = 40;
static uint32_t simLastMs = 0;

static void toLocal(double lat, double lon, float &x, float &y) {
  x = (float)((lon - lon0) * cosLat0 * 111320.0);
  y = (float)((lat - lat0) * 110540.0);
}

static void addPosition(double lat, double lon) {
  if (!haveOrigin) {
    lat0 = lat;
    lon0 = lon;
    cosLat0 = cos(lat * DEG_TO_RAD);
    haveOrigin = true;
  }
  gps.lat = lat;
  gps.lon = lon;
  toLocal(lat, lon, curX, curY);
  if (tn > 0) {
    float dx = curX - tx[tn - 1], dy = curY - ty[tn - 1];
    float d = sqrtf(dx * dx + dy * dy);
    if (d < MIN_STEP_M) return;
    gps.trackKm += d / 1000.0f;
  }
  if (tn == MAX_POINTS) {  // full: keep every other point
    for (int i = 0; i < MAX_POINTS / 2; i++) {
      tx[i] = tx[i * 2];
      ty[i] = ty[i * 2];
    }
    tn = MAX_POINTS / 2;
  }
  tx[tn] = curX;
  ty[tn] = curY;
  tn++;
}

void gps_begin() {
  Serial1.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  simLastMs = millis();
}

void gps_loop(float simSpeedKph) {
  while (Serial1.available()) {
    if (nmea.encode(Serial1.read())) lastNmeaMs = millis();
  }
  const uint32_t now = millis();
  const bool realGps = lastNmeaMs && now - lastNmeaMs < 3000;

  if (realGps) {
    if (gps.simulated) {  // first real data: drop the simulated track
      gps.simulated = false;
      tn = 0;
      haveOrigin = false;
      gps.trackKm = 0;
    }
    gps.sats = nmea.satellites.isValid() ? nmea.satellites.value() : 0;
    gps.fix = nmea.location.isValid() && nmea.location.age() < 3000;
    if (nmea.course.isValid() && nmea.speed.kmph() > 3) gps.headingDeg = nmea.course.deg();
    if (gps.fix && nmea.location.isUpdated()) addPosition(nmea.location.lat(), nmea.location.lng());
    return;
  }

  if (!gps.simulated) {  // had a GPS, lost it
    gps.fix = false;
    gps.sats = 0;
    return;
  }

  // ---- simulated ride: wandering heading at the simulated bike speed ----
  const float dt = (now - simLastMs) / 1000.0f;
  simLastMs = now;
  const float t = now / 1000.0f;
  simHeading += dt * (14.0f * sinf(t * 0.05f) + 6.0f * sinf(t * 0.17f));
  simHeading = fmodf(simHeading + 360.0f, 360.0f);
  const float dist = simSpeedKph / 3.6f * dt * 8.0f;  // ×8 so the track grows visibly
  simLat += dist * cosf(simHeading * DEG_TO_RAD) / 110540.0;
  simLon += dist * sinf(simHeading * DEG_TO_RAD) / (111320.0 * cos(simLat * DEG_TO_RAD));
  gps.fix = true;
  gps.sats = 0;
  gps.headingDeg = simHeading;
  addPosition(simLat, simLon);
}

int gps_trackCount() { return tn; }
void gps_trackPoint(int i, float &x, float &y) {
  x = tx[i];
  y = ty[i];
}
void gps_position(float &x, float &y) {
  x = curX;
  y = curY;
}
