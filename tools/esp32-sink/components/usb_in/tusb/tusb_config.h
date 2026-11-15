// TinyUSB configuration: UAC 1.0 stereo speaker, Full-Speed, ESP32-S3 OTG.
#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_
#include "sdkconfig.h"

#define CFG_TUSB_OS                 OPT_OS_FREERTOS
#define CFG_TUSB_OS_INC_PATH        freertos/
#define CFG_TUSB_DEBUG              0
#define CFG_TUD_ENABLED             1
#define BOARD_TUD_RHPORT            0
#define CFG_TUD_MAX_SPEED           OPT_MODE_FULL_SPEED
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))
#define CFG_TUD_ENDPOINT0_SIZE      64
#define CFG_TUD_DWC2_SLAVE_ENABLE   1
#define CFG_TUD_DWC2_DMA_ENABLE     0

#define CFG_TUD_CDC     0
#define CFG_TUD_MSC     0
#define CFG_TUD_HID     0
#define CFG_TUD_MIDI    0
#define CFG_TUD_VENDOR  0
#define CFG_TUD_AUDIO   1

#define UAC_SAMPLE_RATE         CONFIG_USB_IN_SAMPLE_RATE
#define UAC_N_CHANNELS          CONFIG_USB_IN_CHANNEL_COUNT
#define UAC_BYTES_PER_SAMPLE    (CONFIG_USB_IN_BIT_DEPTH / 8)
#define UAC_RESOLUTION          CONFIG_USB_IN_BIT_DEPTH

#define CFG_TUD_AUDIO_CTRL_BUF_SZ               64
#define CFG_TUD_AUDIO_ENABLE_EP_OUT             1
#define CFG_TUD_AUDIO_ENABLE_EP_IN              0
// Adaptive OUT endpoint, no explicit feedback EP (we discard samples, so no drift to correct)
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP        0
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX      TUD_AUDIO_EP_SIZE(false, UAC_SAMPLE_RATE, UAC_BYTES_PER_SAMPLE, UAC_N_CHANNELS)
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ   (16 * CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX)

#endif
