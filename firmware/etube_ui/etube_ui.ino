/* E-TUBE Tap touchscreen UI — LVGL 9 on the LCDwiki ES3C28P
   (ESP32-S3R8, 2.8" IPS 320x240 ILI9341V over SPI, FT6336U touch over I2C).

   Screens (tab bar or swipe): Ride | Map | Music (iPhone) | Radio | MP3 (microSD).
   Design: shimano-ebike-hack/docs/img (dashboard.png, media-*.svg).

   Ride and media values are SIMULATED. Real inputs so far:
     - iPhone music remote (Apple Media Service) + bike data service over BLE, see iphone.cpp
     - internet radio over Wi-Fi, see radio.cpp; clock from NTP
     - heart rate over BLE (chest strap / watch broadcast / HeartCast), see hr_ble.cpp
     - GPS module on GPIO14 (RX) / GPIO21 (TX), see gps_track.cpp; simulated route until it is fitted.
   Nothing here talks to the bike, Wi-Fi or an SD card yet. The status bar says "SIM DATA" until a real decoder fills
   RideState and sets ride.simulated = false.

   Libraries: lvgl 9.2.2 (with ~/Documents/Arduino/libraries/lv_conf.h), NimBLE-Arduino 2.x, TinyGPSPlus,
              GFX Library for Arduino 1.6.x.
   Arduino IDE: ESP32S3 Dev Module, USB CDC On Boot = Enabled,
                Flash Size = 16MB, PSRAM = OPI PSRAM,
                Partition Scheme = 16M Flash (3MB APP/9.9MB FATFS).
*/
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "state.h"
#include "ui.h"
#include "hr_ble.h"
#include "gps_track.h"
#include "sound.h"
#include "radio.h"
#include "iphone.h"

// ---- ES3C28P pins (vendor User_Setup.h / touch.h; LCD ID read back as 0x9341) ----
constexpr int PIN_LCD_CS = 10, PIN_LCD_MOSI = 11, PIN_LCD_SCK = 12, PIN_LCD_MISO = 13;
constexpr int PIN_LCD_DC = 46, PIN_LCD_BL = 45;
constexpr int PIN_TP_SDA = 16, PIN_TP_SCL = 15, PIN_TP_INT = 17, PIN_TP_RST = 18;
constexpr uint8_t FT6336_ADDR = 0x38;

// Touch → screen mapping for landscape. Adjust if taps land in the wrong place
// (watch "touch raw" in the Serial Monitor).
constexpr bool TOUCH_SWAP_XY = false;  // portrait: touch panel is native 240x320
constexpr bool TOUCH_INVERT_X = false;
constexpr bool TOUCH_INVERT_Y = false;
constexpr bool TOUCH_LOG = false;   // prints every touch; set false when mapping is confirmed

constexpr int W = 240, H = 320;  // portrait

Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCK,
                                            PIN_LCD_MOSI, PIN_LCD_MISO);
// IPS panel: colour inversion ON (without it blue shows as yellow)
Arduino_GFX *gfx = new Arduino_ILI9341(bus, GFX_NOT_DEFINED, 0 /*portrait*/, true /*IPS invert*/);

RideState ride;
MediaState media;
HeartState heart;
PhoneState phone;
GpsState gps;
static uint32_t lastSim = 0, lastUi = 0;  // file-scope: no lazy static init in loop()

// ---------------------------------------------------------------- LVGL glue
static uint32_t tick_cb() { return millis(); }

// Screenshot: 'p' on serial redraws everything into this PSRAM copy, then sends it.
static uint16_t *shot = nullptr;
static bool shotArmed = false;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  const int w = lv_area_get_width(area), h = lv_area_get_height(area);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px, w, h);
  if (shotArmed && shot)
    for (int r = 0; r < h; r++) memcpy(shot + (area->y1 + r) * W + area->x1, (uint16_t *)px + r * w, w * 2);
  lv_display_flush_ready(disp);
}

static bool readTouch(int16_t &sx, int16_t &sy) {
  Wire.beginTransmission(FT6336_ADDR);
  Wire.write(0x02);  // TD_STATUS, P1_XH, P1_XL, P1_YH, P1_YL
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(FT6336_ADDR, (uint8_t)5) != 5) return false;
  uint8_t n = Wire.read() & 0x0F;
  uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  if (n == 0 || n > 2) return false;
  int16_t rx = ((xh & 0x0F) << 8) | xl;
  int16_t ry = ((yh & 0x0F) << 8) | yl;
  int16_t x = TOUCH_SWAP_XY ? ry : rx;
  int16_t y = TOUCH_SWAP_XY ? rx : ry;
  if (TOUCH_INVERT_X) x = W - 1 - x;
  if (TOUCH_INVERT_Y) y = H - 1 - y;
  sx = constrain(x, 0, W - 1);
  sy = constrain(y, 0, H - 1);
  if (TOUCH_LOG) Serial.printf("touch raw (%d,%d) -> (%d,%d)\n", rx, ry, sx, sy);
  return true;
}

// Fake taps from the serial port ("t120,248"), for testing without a finger.
static volatile int16_t fakeX = 0, fakeY = 0;
static volatile uint32_t fakeUntil = 0;

static void touch_cb(lv_indev_t *, lv_indev_data_t *data) {
  static int16_t lastX = 0, lastY = 0;
  int16_t x, y;
  if (millis() < fakeUntil) {
    lastX = fakeX;
    lastY = fakeY;
    data->state = LV_INDEV_STATE_PRESSED;
  } else if (readTouch(x, y)) {
    lastX = x;
    lastY = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
  data->point.x = lastX;
  data->point.y = lastY;
}

// ---------------------------------------------------------------- simulator
static void updateClock();
static void simulate(uint32_t now, uint32_t dtMs) {
  updateClock();
  if (!ride.simulated) return;  // live EN100 data (bike_ble.cpp) owns the ride values
  if (ride.assist == ASSIST_OFF) ride.assist = ASSIST_NORMAL;
  const float t = now / 1000.0f;
  ride.speedKph = max(0.0f, 21.0f + 6.0f * sinf(t * 0.23f) + 1.5f * sinf(t * 1.1f));
  ride.tripKm += ride.speedKph * dtMs / 3600000.0f;
  // battery drains faster in higher assist
  static float bat = 72.0f;
  bat -= dtMs / 1000.0f * (0.004f + 0.004f * (int)ride.assist);
  if (bat < 5) bat = 100;
  ride.batteryPct = (int)bat;
  static const float kmPerPct[3] = {0.95f, 0.67f, 0.48f};
  ride.rangeKm = (int)(bat * kmPerPct[ride.assist]);
}

static void updateClock() {
  // real local time (NTP via Wi-Fi, see radio.cpp); "--:--" until synced
  time_t t_now = time(nullptr);
  struct tm lt;
  localtime_r(&t_now, &lt);
  ride.clockValid = lt.tm_year + 1900 >= 2024;
  ride.hour = lt.tm_hour;
  ride.minute = lt.tm_min;

}

// ---------------------------------------------------------------- setup / loop
static void heapLog(const char *step) {
  Serial.printf("[heap] %-10s internal free %6u  largest %6u  PSRAM free %u\n", step,
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, LOW);  // keep dark until the first frame is drawn

  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);
  delay(10);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(300);
  pinMode(PIN_TP_INT, INPUT);
  Wire.begin(PIN_TP_SDA, PIN_TP_SCL, 400000);

  if (!gfx->begin(40000000)) Serial.println("Display init FAILED");
  gfx->fillScreen(0x0000);

  lv_init();
  lv_tick_set_cb(tick_cb);

  lv_display_t *disp = lv_display_create(W, H);
  // one 20-line draw buffer in internal RAM (Wi-Fi/TLS/BLE need the rest)
  const size_t bufBytes = W * 20 * sizeof(uint16_t);
  void *buf1 = heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  lv_display_set_buffers(disp, buf1, nullptr, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush_cb);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_cb);

  heapLog("lvgl+disp");
  radio_begin();   // creates I2S1 first...
  heapLog("radio");
  sound_begin();   // ...then I2S0 for clicks, which takes the pins until a station plays
  heapLog("sound");
  hr_begin();
  iphone_begin();
  heapLog("ble");
  gps_begin();
  ui_create();
  heapLog("ui");
  lv_timer_handler();
  digitalWrite(PIN_LCD_BL, HIGH);
  Serial.println("E-TUBE UI running (SIMULATED data until the EN100 connects). Tap the tab bar or swipe for other screens.");
  Serial.printf("Heap: internal free %u (largest %u), PSRAM free %u\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  lastSim = millis();
}

void loop() {
  const uint32_t now = millis();
  if (now - lastSim >= 100) {
    simulate(now, now - lastSim);
    gps_loop(ride.speedKph);
    lastSim = now;
  }
  hr_loop();
  iphone_loop();
  // serial test commands: r0 / r1 / r2 = play station, s = stop radio
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 's') radio_stop();
    if (c == 'w') sd_write_test();
    if (c == 'p') {  // screenshot: "SHOT 240 320\n" + raw RGB565 little-endian + "\nEND\n"
      if (!shot) shot = (uint16_t *)ps_malloc(W * H * 2);
      if (shot) {
        shotArmed = true;
        lv_obj_invalidate(lv_screen_active());
        lv_obj_invalidate(lv_layer_top());
        lv_refr_now(nullptr);
        shotArmed = false;
        Serial.printf("SHOT %d %d\n", W, H);
        Serial.flush();
        Serial.write((uint8_t *)shot, W * H * 2);
        Serial.flush();
        Serial.print("\nEND\n");
      }
    }
    if (c == 'c') {  // print the clock
      time_t tn = time(nullptr);
      Serial.printf("clock: %s", ctime(&tn));
    }
    if (c == 't') {  // t<x>,<y>  fake 120 ms tap
      String a = Serial.readStringUntil('\n');
      int comma = a.indexOf(',');
      if (comma > 0) {
        fakeX = a.substring(0, comma).toInt();
        fakeY = a.substring(comma + 1).toInt();
        fakeUntil = millis() + 120;
        Serial.printf("fake tap at %d,%d\n", fakeX, fakeY);
      }
    }
    if (c == 'r') {
      delay(5);
      int n = Serial.read() - '0';
      if (n >= 0 && n < STATION_COUNT) radio_play(n);
    }
  }
  if (now - lastUi >= 100) {  // the guide: update the screen at most 10x per second
    lastUi = now;
    ui_update();
  }
  lv_timer_handler();
  delay(5);
}
