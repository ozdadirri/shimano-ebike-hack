// UI sounds through the ES3C28P's ES8311 codec + FM8002E amplifier.
// Pins from the vendor demo (Example_16_music/ESP_Panel_Board_Custom.h).
// The ES8311 register setup is ported from the vendor es8311.cpp to Arduino Wire,
// because their legacy IDF I2C driver cannot run next to Wire (used for touch).
#include <Arduino.h>
#include <Wire.h>
#include <ESP_I2S.h>
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "sound.h"

constexpr int PIN_I2S_MCLK = 4, PIN_I2S_BCLK = 5, PIN_I2S_WS = 7, PIN_I2S_DOUT = 8;
constexpr int PIN_AMP_EN = 1;  // FM8002E shutdown pin: LOW = amplifier on
constexpr uint8_t ES8311_ADDR = 0x18;
constexpr uint32_t RATE = 16000;  // MCLK = 256 x 16 kHz = 4.096 MHz

static I2SClass i2s(I2S_NUM_0);  // I2S1 belongs to the radio
static volatile bool radioOwnsPins = false;
static QueueHandle_t q;
static bool ok = false;
static int boostDb = 0;  // codec gain above 0 dB (volume boost), 0..12

static bool wr(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static uint8_t rd(uint8_t reg) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  Wire.requestFrom(ES8311_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

static bool codecInit() {
  if (!wr(0x00, 0x1F)) return false;  // reset
  delay(20);
  wr(0x00, 0x00);
  wr(0x00, 0x80);                     // power on
  wr(0x01, 0x3F);                     // all clocks on, MCLK from MCLK pin
  wr(0x06, rd(0x06) & ~0x20);         // SCLK not inverted
  // clock dividers for MCLK 4.096 MHz, 16 kHz (vendor coeff table row)
  wr(0x02, (rd(0x02) & 0x07) | ((1 - 1) << 5) | (0 << 3));  // pre_div 1, pre_multi 1x
  wr(0x03, (0 << 6) | 0x10);          // single speed, ADC OSR
  wr(0x04, 0x10);                     // DAC OSR
  wr(0x05, 0x00);                     // ADC/DAC divider 1
  wr(0x06, (rd(0x06) & 0xE0) | (4 - 1));  // BCLK divider 4
  wr(0x07, (rd(0x07) & 0xC0) | 0x00); // LRCK high
  wr(0x08, 0xFF);                     // LRCK low
  wr(0x00, rd(0x00) & 0xBF);          // slave serial port
  wr(0x09, 3 << 2);                   // SDP in: 16-bit I2S
  wr(0x0A, 3 << 2);                   // SDP out: 16-bit I2S
  wr(0x0D, 0x01);                     // power up analog
  wr(0x0E, 0x02);                     // enable PGA / ADC modulator
  wr(0x12, 0x00);                     // power up DAC
  wr(0x13, 0x10);                     // enable output drive
  wr(0x1C, 0x6A);                     // ADC EQ bypass, DC offset cancel
  wr(0x37, 0x08);                     // bypass DAC EQ
  sound_boost(0);
  return true;
}

// ---- tone synthesis ----
static const int MAX_FRAMES = RATE / 10;  // up to 100 ms per sound
static int16_t buf[MAX_FRAMES * 2];       // stereo

// Sine sweep f0→f1 over ms, with a fast attack and exponential decay.
static size_t tone(int frames0, float f0, float f1, int ms, float amp) {
  int n = min((int)(RATE * ms / 1000), MAX_FRAMES - frames0);
  float phase = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / n;
    float f = f0 + (f1 - f0) * t;
    phase += 2 * PI * f / RATE;
    float env = min(1.0f, i / (RATE * 0.002f)) * expf(-4.0f * t);
    int16_t s = (int16_t)(sinf(phase) * env * amp * 32767);
    buf[(frames0 + i) * 2] = s;
    buf[(frames0 + i) * 2 + 1] = s;
  }
  return frames0 + n;
}

static void play(Sound s) {
  size_t n = 0;
  switch (s) {
    case SOUND_CLICK: n = tone(0, 2200, 1800, 18, 0.45f); break;
    case SOUND_TAB:   n = tone(0, 900, 1300, 35, 0.40f); break;
    case SOUND_TOGGLE:
      n = tone(0, 1300, 1300, 30, 0.40f);
      n = tone(n, 1950, 1950, 40, 0.40f);
      break;
  }
  // pad with a little silence so the DMA flushes the tail
  size_t pad = min((size_t)(RATE / 100), (size_t)MAX_FRAMES - n);
  memset(&buf[n * 2], 0, pad * 4);
  n += pad;
  i2s.write((const uint8_t *)buf, n * 4);
}

static void soundTask(void *) {
  Sound s;
  for (;;) {
    if (xQueueReceive(q, &s, portMAX_DELAY) == pdTRUE && !radioOwnsPins) play(s);
  }
}

bool sound_begin() {
  pinMode(PIN_AMP_EN, OUTPUT);
  digitalWrite(PIN_AMP_EN, HIGH);  // amp off while the codec starts
  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, -1, PIN_I2S_MCLK);
  if (!i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("Sound: I2S start failed");
    return false;
  }
  delay(10);  // MCLK running before the codec is configured
  if (!codecInit()) {
    Serial.println("Sound: ES8311 not answering on I2C 0x18");
    return false;
  }
  digitalWrite(PIN_AMP_EN, LOW);   // amp on
  q = xQueueCreate(4, sizeof(Sound));
  xTaskCreatePinnedToCore(soundTask, "sound", 4096, nullptr, 2, nullptr, 0);
  ok = true;
  Serial.println("Sound: ready");
  return true;
}

void sound_play(Sound s) {
  if (!ok) return;
  xQueueSend(q, &s, 0);  // drop if busy; never block the UI
}

// Hand the I2S pins to the radio (I2S1) or back to the UI sounds (I2S0).
void sound_route_radio(bool radio) {
  radioOwnsPins = radio;
  esp_rom_gpio_connect_out_signal(PIN_I2S_BCLK, radio ? I2S1O_BCK_OUT_IDX : I2S0O_BCK_OUT_IDX, false, false);
  esp_rom_gpio_connect_out_signal(PIN_I2S_WS, radio ? I2S1O_WS_OUT_IDX : I2S0O_WS_OUT_IDX, false, false);
  esp_rom_gpio_connect_out_signal(PIN_I2S_DOUT, radio ? I2S1O_SD_OUT_IDX : I2S0O_SD_OUT_IDX, false, false);
  esp_rom_gpio_connect_out_signal(PIN_I2S_MCLK, radio ? I2S1_MCLK_OUT_IDX : I2S0_MCLK_OUT_IDX, false, false);
  wr(0x09, radio ? (4 << 2) : (3 << 2));  // codec word length: 32-bit (radio) / 16-bit (clicks)
}

// ES8311 DAC volume register 0x32: 0xBF = 0 dB, 0.5 dB per step. 0 dB = loudest without
// digital clipping; the boost goes above that (loud passages may distort).
void sound_boost(int db) {
  boostDb = constrain(db, 0, 12);
  wr(0x32, 0xBF + boostDb * 2);
}

void sound_volume(int pct) {
  pct = constrain(pct, 0, 100);
  wr(0x32, pct == 0 ? 0 : pct * 0xBF / 100);
}
