// Audio player: internet radio over Wi-Fi AND MP3 files from the microSD card
// (ESP32-audioI2S 3.4.x) into the ES8311 codec. One Audio object plays one thing:
// starting a station stops the MP3 and vice versa.
// The player owns I2S1; UI clicks own I2S0. Both share the same four pins, so
// sound_route_radio() switches the pins between them in the GPIO matrix.
// All network/SD/decoder work runs in its own task on core 0, never in the UI loop.
#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <SD_MMC.h>
#include <Audio.h>
#include "radio.h"
#include "sound.h"
#include "state.h"
#include "secrets.h"  // WIFI_SSID / WIFI_PASS, generated from ../../.env

constexpr int PIN_I2S_MCLK = 4, PIN_I2S_BCLK = 5, PIN_I2S_WS = 7, PIN_I2S_DOUT = 8;
// microSD, 4-bit SDMMC (vendor demo Example_16_music)
constexpr int PIN_SD_CLK = 38, PIN_SD_CMD = 40, PIN_SD_D0 = 39, PIN_SD_D1 = 41, PIN_SD_D2 = 48,
              PIN_SD_D3 = 47;

static Audio audio(I2S_NUM_1);
static QueueHandle_t cmdq;
static uint32_t playStartMs = 0;
static volatile bool mp3Eof = false;

enum CmdType : uint8_t { C_STOP, C_STATION, C_VOLUME, C_FILE, C_PAUSE, C_RESCAN };
struct Cmd {
  CmdType type;
  int16_t arg;
};

// ---------------------------------------------------------------- MP3 library (PSRAM)
static const int MAX_FILES = 400;
static const size_t ARENA = 64 * 1024;
static char *arena = nullptr;     // all paths, NUL-separated
static uint32_t *offsets = nullptr;
static int fileCount = 0;
static size_t arenaUsed = 0;

static bool isMp3(const char *name) {
  size_t n = strlen(name);
  return n > 4 && strcasecmp(name + n - 4, ".mp3") == 0 && name[0] != '.';
}

static void scanDir(fs::FS &fs, const char *dir, int depth) {
  File d = fs.open(dir);
  if (!d || !d.isDirectory()) return;
  for (File f = d.openNextFile(); f && fileCount < MAX_FILES; f = d.openNextFile()) {
    const char *path = f.path();
    if (f.isDirectory()) {
      const char *base = strrchr(path, '/');
      if (depth < 3 && base && base[1] != '.' && strcasecmp(base, "/System Volume Information") != 0)
        scanDir(fs, path, depth + 1);
    } else if (isMp3(f.name())) {
      size_t len = strlen(path) + 1;
      if (arenaUsed + len > ARENA) break;
      memcpy(arena + arenaUsed, path, len);
      offsets[fileCount++] = arenaUsed;
      arenaUsed += len;
    }
  }
}

static int cmpPath(const void *a, const void *b) {
  return strcasecmp(arena + *(const uint32_t *)a, arena + *(const uint32_t *)b);
}

static void scanSd() {
  if (!arena) arena = (char *)ps_malloc(ARENA);
  if (!offsets) offsets = (uint32_t *)ps_malloc(MAX_FILES * sizeof(uint32_t));
  fileCount = 0;
  arenaUsed = 0;
  SD_MMC.end();
  // Try fastest first, then fall back: some cards/sockets are unreliable in 4-bit mode.
  struct Mode { bool oneBit; int khz; const char *name; };
  static const Mode modes[] = {{false, SDMMC_FREQ_DEFAULT, "4-bit 20 MHz"},
                               {true, SDMMC_FREQ_DEFAULT, "1-bit 20 MHz"},
                               {true, SDMMC_FREQ_PROBING, "1-bit 400 kHz"}};
  bool ok = false;
  for (const Mode &m : modes) {
    SD_MMC.end();
    if (m.oneBit) SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
    else SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
    if (SD_MMC.begin("/sdcard", m.oneBit, false, m.khz)) {
      Serial.printf("SD: mounted (%s)\n", m.name);
      ok = true;
      break;
    }
    Serial.printf("SD: %s failed\n", m.name);
  }
  if (!ok) {
    Serial.println("SD: no card (or not FAT32/exFAT)");
    media.sdOk = false;
    media.libGen++;
    media.sdScanned = true;
    return;
  }
  media.sdOk = true;
  scanDir(SD_MMC, "/", 0);
  qsort(offsets, fileCount, sizeof(uint32_t), cmpPath);
  media.mp3LibraryCount = fileCount;
  media.libGen++;
  media.sdScanned = true;
  Serial.printf("SD: %llu MB card, %d MP3 files\n", SD_MMC.cardSize() >> 20, fileCount);
}

int mp3_count() { return fileCount; }

const char *mp3_path(int i) { return (i >= 0 && i < fileCount) ? arena + offsets[i] : ""; }

// "/Music/Artist - Song.mp3" -> "Artist - Song"
void mp3_name(int i, char *out, size_t n) {
  const char *p = mp3_path(i);
  const char *base = strrchr(p, '/');
  base = base ? base + 1 : p;
  size_t len = strlen(base);
  if (len > 4) len -= 4;
  len = min(len, n - 1);
  memcpy(out, base, len);
  out[len] = 0;
}

// ---------------------------------------------------------------- player
static void setTitle(const char *s) { strlcpy(media.radioTitle, s ? s : "", sizeof media.radioTitle); }

static void stopAll() {
  audio.stopSong();
  sound_route_radio(false);
  media.radioState = RADIO_IDLE;
  media.bufferPct = 0;
  media.mp3Playing = false;
  media.mp3Paused = false;
  setTitle("");
}

static void startStation(int i) {
  media.station = i;
  media.mp3Playing = media.mp3Paused = false;
  setTitle("");
  if (WiFi.status() != WL_CONNECTED) {
    media.radioState = RADIO_NO_WIFI;
    Serial.println("Radio: no Wi-Fi yet");
    return;
  }
  audio.stopSong();
  sound_route_radio(true);
  media.radioState = RADIO_CONNECTING;
  media.radioOffline = false;
  media.bufferPct = 0;
  Serial.printf("Radio: connecting to %s\n", STATIONS[i].url);
  if (audio.connecttohost(STATIONS[i].url)) {
    if (media.radioState != RADIO_CONNECTING) {  // user stopped/switched while connecting
      Serial.println("Radio: cancelled while connecting");
      if (media.radioState == RADIO_IDLE) stopAll();
      return;  // a newer command is waiting in the queue
    }
    media.radioState = RADIO_PLAYING;
    playStartMs = millis();
  } else {
    media.radioState = RADIO_ERROR;
    sound_route_radio(false);
    Serial.println("Radio: connect failed");
  }
}

static void startFile(int i) {
  if (i < 0 || i >= fileCount) return;
  media.radioState = RADIO_IDLE;
  media.mp3Track = i;
  media.mp3PosS = media.mp3DurS = 0;
  audio.stopSong();
  sound_route_radio(true);
  mp3Eof = false;
  Serial.printf("MP3: %s\n", mp3_path(i));
  if (audio.connecttoFS(SD_MMC, mp3_path(i))) {
    media.mp3Playing = true;
    media.mp3Paused = false;
  } else {
    Serial.println("MP3: cannot play this file");
    media.mp3Playing = false;
    sound_route_radio(false);
  }
}

static int nextTrack(int d) {
  if (fileCount == 0) return -1;
  if (media.shuffle && fileCount > 1) {
    int n;
    do n = random(fileCount); while (n == media.mp3Track);
    return n;
  }
  return (media.mp3Track + d + fileCount) % fileCount;
}

static void playerTask(void *) {
  scanSd();  // on this task so a slow card never delays the screen
  Cmd cmd;
  uint32_t lastPoll = 0;
  for (;;) {
    // Drain the queue: apply volume at once, but only act on the LAST play/stop,
    // so quick taps don't start every track/station in between.
    Cmd last = {C_VOLUME, -1};
    bool have = false;
    while (xQueueReceive(cmdq, &cmd, 0) == pdTRUE) {
      if (cmd.type == C_VOLUME) {  // 0..21 = player volume, 22..27 = +2 dB codec boost each
        audio.setVolume(min((int)cmd.arg, 21));
        sound_boost(max(0, cmd.arg - 21) * 2);
      }
      else if (cmd.type == C_RESCAN) {
        if (!media.mp3Playing) scanSd();
        else media.sdScanned = true;  // can't remount while a song plays; keep the old list
      } else if (cmd.type == C_PAUSE) {
        if (media.mp3Playing && audio.pauseResume()) media.mp3Paused = !media.mp3Paused;
      } else {
        last = cmd;
        have = true;
      }
    }
    if (have) {
      if (last.type == C_STOP) stopAll();
      else if (last.type == C_STATION) startStation(last.arg);
      else if (last.type == C_FILE) startFile(last.arg);
    }
    audio.loop();

    if (mp3Eof) {  // end of an MP3: repeat, shuffle or next
      mp3Eof = false;
      if (media.mp3Playing) startFile(media.repeat ? media.mp3Track : nextTrack(1));
    }

    const uint32_t now = millis();
    media.wifiOk = WiFi.status() == WL_CONNECTED;
    if (now - lastPoll > 250) {
      lastPoll = now;
      if (media.mp3Playing) {
        media.mp3PosS = audio.getAudioCurrentTime();
        media.mp3DurS = audio.getAudioFileDuration();
      }
      if (media.radioState == RADIO_PLAYING) {
        uint32_t size = audio.getInBufferSize();
        media.bufferPct = size ? (int)(100ULL * audio.inBufferFilled() / size) : 0;
        // stream died (server closed, decode error, network drop)
        if (!audio.isRunning() && now - playStartMs > 3000) {
          Serial.println("Radio: stream stopped");
          media.radioState = RADIO_ERROR;
          sound_route_radio(false);
        }
      }
    }
    vTaskDelay(1);
  }
}

void radio_begin() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);  // connects in the background
  Serial.printf("Wi-Fi: connecting to %s\n", WIFI_SSID);
  // Real clock from the internet once Wi-Fi is up. Sydney time with daylight saving.
  configTzTime("AEST-10AEDT,M10.1.0,M4.1.0/3", "pool.ntp.org", "time.google.com");

  Audio::audio_info_callback = [](Audio::msg_t m) {
    // the library reports HTTP errors through the title event ("HTTP/1.1 530 ...")
    if (m.e == Audio::evt_streamtitle && m.msg && strncmp(m.msg, "HTTP/", 5) == 0) {
      media.radioOffline = true;
      return;
    }
    if (m.e == Audio::evt_streamtitle) setTitle(m.msg);
    if (m.e == Audio::evt_eof) mp3Eof = true;
    if (m.e != Audio::evt_log) Serial.printf("Audio %s: %s\n", m.s ? m.s : "", m.msg ? m.msg : "");
  };
  // default 32x256 frames = 64 KB of internal DMA RAM; the 640 KB stream buffer is in PSRAM anyway
  audio.settings.DMA_DESC_NUM = 8;
  audio.settings.VU_LEVEL = false;
  audio.setPinout(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_MCLK);  // creates I2S1
  audio.setVolume(media.radioVolume);

  cmdq = xQueueCreate(8, sizeof(Cmd));
  // stack in PSRAM: internal RAM is needed for Wi-Fi/TLS (this task never writes internal flash)
  xTaskCreatePinnedToCoreWithCaps(playerTask, "player", 12288, nullptr, 3, nullptr, 0,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void send(CmdType t, int arg) {
  Cmd c = {t, (int16_t)arg};
  xQueueSend(cmdq, &c, 0);
}

void radio_play(int station) {
  media.station = station;
  media.radioState = RADIO_CONNECTING;  // show it on screen right away
  media.mp3Playing = false;
  send(C_STATION, station);
}

void radio_volume(int v) { send(C_VOLUME, constrain(v, 0, RADIO_VOL_MAX)); }

void radio_stop() {
  media.radioState = RADIO_IDLE;  // show "stopped" right away
  media.bufferPct = 0;
  media.mp3Playing = false;
  send(C_STOP, 0);
}

void mp3_play(int i) {
  if (i < 0 || i >= fileCount) return;
  media.mp3Track = i;
  media.mp3Playing = true;
  media.mp3Paused = false;
  media.radioState = RADIO_IDLE;
  send(C_FILE, i);
}

void mp3_toggle() {
  if (media.mp3Playing) send(C_PAUSE, 0);
  else mp3_play(media.mp3Track);
}

void mp3_step(int d) { mp3_play(nextTrack(d)); }

// Write test: create a file, remount the card, and check it is still there.
void sd_write_test() {
  if (!media.sdOk) { Serial.println("SDTEST: no card mounted"); return; }
  File f = SD_MMC.open("/board_write_test.txt", FILE_WRITE);
  if (!f) { Serial.println("SDTEST: cannot create file"); return; }
  size_t n = f.print("written by the ESP32 board\n");
  f.close();
  Serial.printf("SDTEST: wrote %u bytes\n", (unsigned)n);
  SD_MMC.end();
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
  if (!SD_MMC.begin("/sdcard", false)) { Serial.println("SDTEST: remount failed"); return; }
  File r = SD_MMC.open("/board_write_test.txt");
  Serial.printf("SDTEST: after remount the file %s (%u bytes)\n", r ? "EXISTS" : "IS GONE", r ? (unsigned)r.size() : 0);
  if (r) r.close();
}

void mp3_rescan() {
  media.sdScanned = false;
  send(C_RESCAN, 0);
}
