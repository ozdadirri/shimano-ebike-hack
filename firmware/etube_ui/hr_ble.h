#pragma once
void hr_begin();  // start BLE; call once in setup()
void hr_loop();   // scan / connect / reconnect; call often from loop()
