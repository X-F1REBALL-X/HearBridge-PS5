// ESP32-S3 PS5 UAC1 sink: enumerates as a generic USB Audio Class 1.0
// speaker (48 kHz / 16-bit / stereo) on the native USB-OTG port, drains and
// discards the PCM stream, and logs the peak level once per second on UART0.
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "usb_in.h"

static const char *TAG = "UAC";

static void init_done(void) { ESP_LOGI(TAG, "usb_in init done"); }

static void drain_task(void *arg)
{
    RingbufHandle_t rb = usb_in_get_ringbuf();
    int peak = 0;
    uint32_t bytes = 0;
    int64_t last = esp_timer_get_time();

    for (;;) {
        size_t n = 0;
        int16_t *p = (int16_t *)xRingbufferReceiveUpTo(rb, &n, pdMS_TO_TICKS(20), 4096);
        if (p && n) {
            size_t samples = n / sizeof(int16_t);
            for (size_t i = 0; i < samples; i++) {
                int v = abs((int)p[i]);
                if (v > peak) peak = v;
            }
            bytes += n;
            vRingbufferReturnItem(rb, p);   // discard
        }
        int64_t now = esp_timer_get_time();
        if (now - last >= 1000000) {
            usb_in_stats_t st;
            usb_in_get_stats(&st);
            // Expect ~192000 B/s (48000 Hz * 2 ch * 2 B) while the PS5 streams
            ESP_LOGI(TAG, "connected=%d streaming=%d rate=%lu bytes/s=%lu peak=%d mute=%d vol=%d/256dB dropped=%lu",
                     st.mounted, st.streaming, (unsigned long)st.sample_rate, (unsigned long)bytes, peak,
                     st.mute, st.volume_db256, (unsigned long)st.dropped_bytes);
            peak = 0;
            bytes = 0;
            last = now;
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Starting PS5 UAC1 sink (48k/16/2)");
    ESP_ERROR_CHECK(usb_in_init(init_done));
    ESP_ERROR_CHECK(usb_in_start());

    xTaskCreatePinnedToCore(drain_task, "uac_drain", 4096, NULL, 6, NULL, 0);
}
