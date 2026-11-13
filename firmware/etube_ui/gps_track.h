#pragma once
#include <stdint.h>

// GPS module on the P3/P4 expansion pins: module TX → GPIO14, module RX ← GPIO21.
// If no NMEA arrives, a simulated ride is generated so the map can be tested.
void gps_begin();
void gps_loop(float simSpeedKph);  // call ~10x per second

// Recorded track in metres, relative to the first fix (x = east, y = north).
int gps_trackCount();
void gps_trackPoint(int i, float &x, float &y);
void gps_position(float &x, float &y);  // current position, same frame
