#pragma once
#include <NimBLEDevice.h>

// Shimano EW-EN100 link (read-only). The shared BLE scan in hr_ble.cpp hands every
// advertisement to bike_onAdvert(); bike_loop() connects and decodes.
bool bike_onAdvert(const NimBLEAdvertisedDevice *dev);  // true if it was the bike (scan can stop)
bool bike_wantScan();                                     // still looking for the bike?
void bike_loop();                                         // call often from loop()
