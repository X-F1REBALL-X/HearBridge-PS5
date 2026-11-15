#ifndef USB_IN_H
#define USB_IN_H
// USB Audio Class 1.0 speaker sink (host -> device PCM), TinyUSB 0.21 backend.
// API kept compatible with netham45/esp32-usb-in.
#include "sdkconfig.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_SAMPLE_RATE      CONFIG_USB_IN_SAMPLE_RATE
#define USB_CHANNEL_NUM      CONFIG_USB_IN_CHANNEL_COUNT
#define USB_BIT_DEPTH        CONFIG_USB_IN_BIT_DEPTH
#define USB_BYTES_PER_SAMPLE (USB_BIT_DEPTH / 8)
#define USB_PCM_BUFFER_SIZE  CONFIG_USB_IN_PCM_BUFFER_SIZE

extern RingbufHandle_t usb_in_pcm_buffer;

typedef struct {
    bool     mounted;        // enumerated + configured by host
    bool     streaming;      // host selected alt setting 1 (stream open)
    uint32_t sample_rate;    // last SET_CUR sampling frequency (Hz)
    bool     mute;           // master mute from host
    int16_t  volume_db256;   // master volume in 1/256 dB
    uint32_t rx_bytes;       // total PCM bytes received from host
    uint32_t dropped_bytes;  // bytes dropped because ring buffer was full
    uint32_t alt_changes;    // SET_INTERFACE count on the streaming itf
} usb_in_stats_t;

esp_err_t usb_in_init(void (*init_done_cb)(void));
esp_err_t usb_in_start(void);
esp_err_t usb_in_stop(void);
void      usb_in_deinit(void);
uint32_t  usb_in_get_sample_rate(void);
bool      usb_in_is_connected(void);   // true when host has mounted the device
void      usb_in_get_stats(usb_in_stats_t *out);

static inline int usb_in_read(uint8_t *buffer, size_t size)
{
    if (!usb_in_pcm_buffer) return 0;
    size_t received_size = 0;
    uint8_t *data = (uint8_t *)xRingbufferReceiveUpTo(usb_in_pcm_buffer, &received_size, pdMS_TO_TICKS(10), size);
    if (data && received_size > 0) {
        memcpy(buffer, data, received_size);
        vRingbufferReturnItem(usb_in_pcm_buffer, (void *)data);
        return (int)received_size;
    }
    return 0;
}

static inline RingbufHandle_t usb_in_get_ringbuf(void) { return usb_in_pcm_buffer; }

#ifdef __cplusplus
}
#endif
#endif
