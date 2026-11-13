#pragma once

enum Sound { SOUND_CLICK, SOUND_TAB, SOUND_TOGGLE };

bool sound_begin();          // after Wire.begin(); starts I2S + ES8311 + amplifier
void sound_play(Sound s);    // non-blocking
void sound_volume(int pct);  // 0..100 (codec DAC volume, 100 = 0 dB)
void sound_boost(int db);    // 0..12 dB above full scale (may distort)
void sound_route_radio(bool radio);  // used by radio.cpp
