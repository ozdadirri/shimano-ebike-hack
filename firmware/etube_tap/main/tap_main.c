// main/tap_main.c: E-TUBE tap, Stage A capture firmware
// Receives frames on GPIO4 with RMT and prints pulse widths in ns over USB:
//   F <symbols> <w0> <w1> ...
// tools/etube_decode.py turns these lines into bits.
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/rmt_rx.h"
#include "driver/ledc.h"
#include "esp_err.h"

#define TAP_GPIO       4            // XIAO D3, comparator output
#define TEST_GPIO      2            // XIAO D1, 1 MHz self-test output (Phase 4 only)
#define RMT_RES_HZ     80000000     // 12.5 ns per tick
#define RX_SYMBOLS     2048

static rmt_channel_handle_t rx_chan;
static rmt_symbol_word_t rx_buf[RX_SYMBOLS];
static QueueHandle_t rx_queue;

static const rmt_receive_config_t rx_cfg = {
    .signal_range_min_ns = 100,     // drop glitches shorter than 100 ns
    .signal_range_max_ns = 20000,   // 20 us without an edge ends the frame
};

static bool IRAM_ATTR on_rx_done(rmt_channel_handle_t ch,
                                 const rmt_rx_done_event_data_t *ev, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(rx_queue, ev, &woken);
    return woken == pdTRUE;
}

static void tap_init(void)
{
    rmt_rx_channel_config_t cfg = {
        .gpio_num = TAP_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RES_HZ,
        .mem_block_symbols = RX_SYMBOLS,
        .flags.with_dma = true,     // ESP32-S3 supports DMA on RMT RX
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&cfg, &rx_chan));
    rx_queue = xQueueCreate(4, sizeof(rmt_rx_done_event_data_t));
    rmt_rx_event_callbacks_t cbs = { .on_recv_done = on_rx_done };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &cbs, NULL));
    ESP_ERROR_CHECK(rmt_enable(rx_chan));
}

#ifdef CONFIG_TAP_SELFTEST
// Phase 4 self-test: 1 MHz, 50 % square wave on TEST_GPIO
static void test_signal_start(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_4_BIT, .freq_hz = 1000000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num = TEST_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 8,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}
#endif

static void tap_task(void *arg)
{
    rmt_rx_done_event_data_t ev;
    for (;;) {
        ESP_ERROR_CHECK(rmt_receive(rx_chan, rx_buf, sizeof(rx_buf), &rx_cfg));
        xQueueReceive(rx_queue, &ev, portMAX_DELAY);
        printf("F %u", (unsigned)ev.num_symbols);
        for (size_t i = 0; i < ev.num_symbols; i++) {
            const rmt_symbol_word_t *s = &ev.received_symbols[i];
            // ticks * 12.5 ns; the last symbol's duration1 may be 0
            printf(" %u %u", (unsigned)(s->duration0 * 25 / 2), (unsigned)(s->duration1 * 25 / 2));
        }
        printf("\n");
    }
}

void app_main(void)
{
    tap_init();
#ifdef CONFIG_TAP_SELFTEST
    test_signal_start();
#endif
    xTaskCreatePinnedToCore(tap_task, "tap", 4096, NULL, 10, NULL, 1);
}
