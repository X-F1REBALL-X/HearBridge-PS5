// USB descriptors: generic USB Audio Class 1.0 (bcdADC 0x0100) stereo speaker,
// 48 kHz / 16-bit PCM, adaptive isochronous OUT endpoint, Full-Speed.
#include <string.h>
#include "sdkconfig.h"
#include "tusb.h"

#define ITF_NUM_AUDIO_CONTROL   0
#define ITF_NUM_AUDIO_STREAMING 1
#define ITF_NUM_TOTAL           2
#define EPNUM_AUDIO_OUT         0x01

#define UAC1_ENTITY_INPUT_TERMINAL  0x01
#define UAC1_ENTITY_FEATURE_UNIT    0x02
#define UAC1_ENTITY_OUTPUT_TERMINAL 0x03

static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    // UAC1 does not use IAD; class is declared per interface
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = CONFIG_UAC_SINK_VID,
    .idProduct          = CONFIG_UAC_SINK_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&desc_device; }

#define UAC1_SPK_DESC_LEN ( \
      TUD_AUDIO10_DESC_STD_AC_LEN \
    + TUD_AUDIO10_DESC_CS_AC_LEN(1) \
    + TUD_AUDIO10_DESC_INPUT_TERM_LEN \
    + TUD_AUDIO10_DESC_FEATURE_UNIT_LEN(2) \
    + TUD_AUDIO10_DESC_OUTPUT_TERM_LEN \
    + TUD_AUDIO10_DESC_STD_AS_LEN \
    + TUD_AUDIO10_DESC_STD_AS_LEN \
    + TUD_AUDIO10_DESC_CS_AS_INT_LEN \
    + TUD_AUDIO10_DESC_TYPE_I_FORMAT_LEN(1) \
    + TUD_AUDIO10_DESC_STD_AS_ISO_EP_LEN \
    + TUD_AUDIO10_DESC_CS_AS_ISO_EP_LEN)

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + UAC1_SPK_DESC_LEN)

#define FU_CTRL (AUDIO10_FU_CONTROL_BM_MUTE | AUDIO10_FU_CONTROL_BM_VOLUME)

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // Standard AC interface
    TUD_AUDIO10_DESC_STD_AC(ITF_NUM_AUDIO_CONTROL, 0x00, 0x04),
    // CS AC header, bcdADC = 1.00, one streaming interface
    TUD_AUDIO10_DESC_CS_AC(0x0100,
        (TUD_AUDIO10_DESC_INPUT_TERM_LEN + TUD_AUDIO10_DESC_FEATURE_UNIT_LEN(2) + TUD_AUDIO10_DESC_OUTPUT_TERM_LEN),
        ITF_NUM_AUDIO_STREAMING),
    // USB streaming -> Feature Unit -> Speaker
    TUD_AUDIO10_DESC_INPUT_TERM(UAC1_ENTITY_INPUT_TERMINAL, AUDIO_TERM_TYPE_USB_STREAMING, 0x00, 0x02,
        AUDIO10_CHANNEL_CONFIG_LEFT_FRONT | AUDIO10_CHANNEL_CONFIG_RIGHT_FRONT, 0x00, 0x00),
    TUD_AUDIO10_DESC_FEATURE_UNIT(UAC1_ENTITY_FEATURE_UNIT, UAC1_ENTITY_INPUT_TERMINAL, 0x00, FU_CTRL, FU_CTRL, FU_CTRL),
    TUD_AUDIO10_DESC_OUTPUT_TERM(UAC1_ENTITY_OUTPUT_TERMINAL, AUDIO_TERM_TYPE_OUT_GENERIC_SPEAKER, 0x00, UAC1_ENTITY_FEATURE_UNIT, 0x00),

    // AS interface alt 0 (zero bandwidth)
    TUD_AUDIO10_DESC_STD_AS_INT(ITF_NUM_AUDIO_STREAMING, 0x00, 0x00, 0x00),
    // AS interface alt 1 (streaming, 1 endpoint)
    TUD_AUDIO10_DESC_STD_AS_INT(ITF_NUM_AUDIO_STREAMING, 0x01, 0x01, 0x00),
    TUD_AUDIO10_DESC_CS_AS_INT(UAC1_ENTITY_INPUT_TERMINAL, 0x01, AUDIO10_DATA_FORMAT_TYPE_I_PCM),
    TUD_AUDIO10_DESC_TYPE_I_FORMAT(UAC_N_CHANNELS, UAC_BYTES_PER_SAMPLE, UAC_RESOLUTION, UAC_SAMPLE_RATE),
    // Isochronous OUT, adaptive, 1 ms interval, no sync endpoint
    TUD_AUDIO10_DESC_STD_AS_ISO_EP(EPNUM_AUDIO_OUT,
        (uint8_t)(TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ADAPTIVE),
        CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX, 0x01, 0x00),
    TUD_AUDIO10_DESC_CS_AS_ISO_EP(AUDIO10_CS_AS_ISO_DATA_EP_ATT_SAMPLING_FRQ,
        AUDIO10_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0x0000),
};

TU_VERIFY_STATIC(sizeof(desc_configuration) == CONFIG_TOTAL_LEN, "Incorrect size");

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},     // 0: English (0x0409)
    CONFIG_UAC_SINK_MANUFACTURER,   // 1
    CONFIG_UAC_SINK_PRODUCT,        // 2
    CONFIG_UAC_SINK_SERIAL,         // 3
    "Speaker",                      // 4: audio control interface
};

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    size_t chr_count;
    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
        const char *str = string_desc_arr[index];
        chr_count = strlen(str);
        if (chr_count > 32) chr_count = 32;
        for (size_t i = 0; i < chr_count; i++) _desc_str[1 + i] = str[i];
    }
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
