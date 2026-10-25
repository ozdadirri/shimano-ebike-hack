// main/buttons.h: press the assist switch's buttons through MCP23008 + PhotoMOS
#pragma once
#include "driver/i2c_master.h"

enum { BTN_UP = 0, BTN_DOWN = 1, BTN_LIGHT = 2 };

// bus: the I2C bus handle your touch driver already created
void buttons_init(i2c_master_bus_handle_t bus);

// 150 ms = short press. Never use this for walk assist.
void button_press(int btn, int ms);
