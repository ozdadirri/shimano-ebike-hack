#pragma once
#include <stddef.h>

// One audio player for internet radio (Wi-Fi) and MP3 files (microSD).
void radio_begin();             // Wi-Fi + player task + SD scan; call BEFORE sound_begin()
void radio_play(int station);   // index into STATIONS[]; non-blocking
void radio_stop();              // stops radio or MP3; non-blocking
static const int RADIO_VOL_MAX = 27;  // 0..21 normal, 22..27 boost (+2 dB each)
void radio_volume(int v);       // 0..RADIO_VOL_MAX, shared by radio and MP3

// MP3 library (all .mp3 files on the card, up to 3 folders deep, sorted by path)
int mp3_count();
const char *mp3_path(int i);
void mp3_name(int i, char *out, size_t n);  // file name without folder / ".mp3"
void mp3_play(int i);
void mp3_toggle();              // pause / resume, or start the selected track
void sd_write_test();          // serial 'w': does a write survive a remount?
void mp3_rescan();             // look for a card again (e.g. inserted after boot)
void mp3_step(int d);           // +1 next, -1 previous (respects shuffle)
