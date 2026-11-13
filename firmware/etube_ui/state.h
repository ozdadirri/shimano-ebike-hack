// Single source of truth for everything the screens show.
// Today it is filled by the simulator in etube_ui.ino. Later the E-TUBE decoder
// (ride values) and the BLE/Wi-Fi media code fill the same structs, and the UI
// does not change.
#pragma once
#include <stdint.h>

// 0..2 index the three mode buttons; OFF lights none of them.
enum Assist : uint8_t { ASSIST_ECO = 0, ASSIST_NORMAL = 1, ASSIST_HIGH = 2, ASSIST_OFF = 3 };

struct RideState {
  bool simulated = true;      // true = sample data, shown as "SIM DATA" in the status bar
  bool busOk = false;         // decoder sees valid frames
  uint32_t busErrors = 0;
  float speedKph = 0.0f;
  Assist assist = ASSIST_NORMAL;
  int batteryPct = 72;
  int rangeKm = 48;
  float tripKm = 12.6f;
  // from the EN100 only (bike_ble.cpp); not shown on screen yet
  int cadenceRpm = 0;
  float dist2Km = 0;  // second distance counter in message 0x03, meaning unknown (not the odometer)
  float avgKph = 0, maxKph = 0;
  bool lightOn = false;
  bool bleConnected = false;  // EN100 link up
  bool clockValid = false;   // true once synced from the internet (NTP)
  uint8_t hour = 0, minute = 0;
};

enum RadioState { RADIO_IDLE, RADIO_NO_WIFI, RADIO_CONNECTING, RADIO_PLAYING, RADIO_ERROR };

struct Track {
  const char *title;
  const char *artist;
  const char *album;
  uint32_t lengthMs;
};

struct Station {
  const char *name;
  const char *info;    // country · language · tags
  const char *format;  // codec / bitrate
  const char *url;     // stream URL (from Radio Browser, as used by the world-radio app)
};

struct MediaState {
  bool playing = true;
  int volumePct = 60;
  // Phone (Spotify) source
  int phoneTrack = 0;
  uint32_t phonePosMs = 102000;  // 1:42
  // Internet radio
  int station = 0;
  int bufferPct = 0;
  bool radioPlaying = false;  // derived from radioState (kept for older code)
  int radioState = 0;         // RadioState
  bool wifiOk = false;
  bool radioOffline = false;  // station's server answered with an HTTP error
  int radioVolume = 12;       // 0..21 (audio library steps)
  char radioTitle[80] = "";   // ICY stream title, if the station sends one
  // MP3 from microSD
  bool sdOk = false;
  bool sdScanned = false;     // SD scan finished (card or not)
  int mp3Track = 0;
  bool mp3Playing = false;    // a file is loaded (playing or paused)
  bool mp3Paused = false;
  uint32_t mp3PosS = 0, mp3DurS = 0;
  bool shuffle = false;
  bool repeat = false;
  int mp3LibraryCount = 0;
  int libGen = 0;             // +1 after every SD scan (UI rebuilds the list)
};

// Sample content (made-up names, as in the design mock-ups)
static const Track PHONE_TRACKS[] = {
    {"Sample Song", "Sample Artist", "Sample Album", 238000},
    {"Another Song", "Sample Artist", "Sample Album", 201000},
    {"Third Song", "Other Artist", "Other Album", 185000},
};
static const int PHONE_TRACK_COUNT = sizeof(PHONE_TRACKS) / sizeof(PHONE_TRACKS[0]);

// Built-in stations (from Radio Browser).
static const Station STATIONS[] = {
    {"\u5546\u696D\u96FB\u53F0\u53F1\u54A4903", "Hong Kong \xC2\xB7 cantonese \xC2\xB7 oldies", "AAC 28k",
     "https://881903.fishtv.dpdns.org/proxy/903"},  // HLS (.m3u8) via a third-party proxy
    {"\u5317\u4EAC\u97F3\u4E50\u5E7F\u64AD", "Beijing \xC2\xB7 chinese \xC2\xB7 97.4 FM", "MP3",
     "https://lhttp.qtfm.cn/live/332/64k.mp3"},
    // StreamTheWorld streams use plain http: public audio, and TLS costs ~35 KB of internal RAM
    {"LOVE 972", "Singapore \xC2\xB7 chinese \xC2\xB7 melisten", "AAC+ 64k",
     "http://playerservices.streamtheworld.com/api/livestream-redirect/LOVE972FM_PREM.aac"},
    {"Sky News Australia Radio", "Sydney NSW \xC2\xB7 english \xC2\xB7 news", "AAC+ 64k",
     "http://playerservices.streamtheworld.com/api/livestream-redirect/NOVA_SKYNEWSAAC.aac"},
    {"BBC Radio 1", "United Kingdom \xC2\xB7 english \xC2\xB7 pop", "AAC+ 101k",  // HLS, international feed
     "http://a.files.bbci.co.uk/ms6/live/3441A116-B12E-4D2F-ACA8-C1984642FA4B/audio/simulcast/hls/nonuk/pc_hd_abr_v2/ak/bbc_radio_one.m3u8"},
    {"ABC News Radio", "NSW \xC2\xB7 english \xC2\xB7 news", "AAC+ 56k",
     "http://abc.streamguys1.com/live/newsradio/icecast.audio"},
    {"Smooth FM 95.3", "Sydney NSW \xC2\xB7 easy listening", "AAC+ 48k",
     "http://playerservices.streamtheworld.com/api/livestream-redirect/SMOOTH953_AAC48.aac"},
    // ABC HLS: the lighter HE-AAC v2 variant (~112k) of the playlist, over plain http
    {"ABC Jazz", "Australia \xC2\xB7 jazz \xC2\xB7 blues", "AAC 112k",
     "http://mediaserviceslive.akamaized.net/hls/live/2108543/abcjazz/v0-222.m3u8"},
    {"ABC Classic", "Sydney NSW \xC2\xB7 classical", "AAC 112k",
     "http://mediaserviceslive.akamaized.net/hls/live/2108547/classicfmnsw/v0-222.m3u8"},
};
static const int STATION_COUNT = sizeof(STATIONS) / sizeof(STATIONS[0]);


extern RideState ride;
extern MediaState media;

// Heart rate from a BLE Heart Rate Service (0x180D) sender: chest strap,
// Garmin "broadcast HR", or Apple Watch via HeartCast on the iPhone.
struct HeartState {
  bool connected = false;
  bool scanning = false;
  int bpm = 0;              // 0 = no reading
  uint32_t lastMs = 0;      // millis() of last reading
  char name[24] = "";
};

// Position + recorded breadcrumb track.
struct GpsState {
  bool simulated = true;    // true until a real GPS module sends NMEA
  bool fix = false;
  int sats = 0;
  double lat = 0, lon = 0;
  float headingDeg = 0;     // 0 = north, clockwise
  float trackKm = 0;
};

// What the iPhone is playing (Apple Media Service), filled by iphone.cpp.
struct PhoneState {
  bool connected = false;   // iPhone connected to this board
  bool paired = false;      // link encrypted (needed for AMS)
  bool ams = false;         // music remote ready
  char player[32] = "";     // e.g. "Spotify", "Music"
  char title[96] = "";
  char artist[64] = "";
  char album[64] = "";
  float durationS = 0;
  float elapsedS = 0;       // position at elapsedAtMs
  uint32_t elapsedAtMs = 0;
  float rate = 0;
  bool playing = false;
  int volumePct = -1;       // -1 = unknown
};

extern HeartState heart;
extern PhoneState phone;
extern GpsState gps;
