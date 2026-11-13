#pragma once
#include <stdint.h>

// Apple Media Service remote commands
enum : uint8_t {
  AMS_PLAY = 0, AMS_PAUSE = 1, AMS_TOGGLE = 2, AMS_NEXT = 3, AMS_PREV = 4,
  AMS_VOL_UP = 5, AMS_VOL_DOWN = 6,
};

void iphone_begin();               // after hr_begin() (which initialises NimBLE)
void iphone_loop();                // call from loop()
void iphone_cmd(uint8_t amsCommand);
