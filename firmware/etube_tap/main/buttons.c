// main/buttons.c: press the switch's buttons through MCP23008 + PhotoMOS
// (guide Chapter 12). Not called from app_main yet: wire it to your UI once the
// tap decodes assist mode, so every press can be checked against the bus.
#include "buttons.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

#define MCP23008_ADDR  0x20
#define REG_IODIR      0x00
#define REG_OLAT       0x0A

static i2c_master_dev_handle_t mcp;

static void mcp_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    ESP_ERROR_CHECK(i2c_master_transmit(mcp, buf, sizeof(buf), 50));
}

void buttons_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MCP23008_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev, &mcp));
    mcp_write(REG_OLAT, 0x00);      // all relays open before enabling outputs
    mcp_write(REG_IODIR, 0xF8);     // GP0..GP2 outputs, rest inputs
}

void button_press(int btn, int ms)
{
    mcp_write(REG_OLAT, (uint8_t)(1u << btn));
    vTaskDelay(pdMS_TO_TICKS(ms));
    mcp_write(REG_OLAT, 0x00);
}
