#pragma once
#include "state.h"

void ui_create();   // build all screens once, after LVGL and the display are up
void ui_update();   // copy RideState/MediaState into the widgets (call ~10x per second)
