// UAC 1.0 speaker sink on ESP32-S3 USB-OTG (TinyUSB >= 0.20 native UAC1).
// Receives isochronous PCM from the host and pushes it into a FreeRTOS byte ring buffer.
#include "usb_in.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/task.h"
#include "esp_private/usb_phy.h"
#include "tusb.h"

static const char *TAG = "usb_in";

RingbufHandle_t usb_in_pcm_buffer = NULL;

#define UAC1_ENTITY_FEATURE_UNIT 0x02
#define ITF_NUM_AUDIO_STREAMING  1

static usb_phy_handle_t s_phy;
static TaskHandle_t s_tud_task;
static TaskHandle_t s_rx_task;
static volatile bool s_running;
static bool s_initialized;
static void (*s_init_done_cb)(void);

static volatile usb_in_stats_t s_st = {
    .sample_rate = USB_SAMPLE_RATE,
    .volume_db256 = 0,
};
static uint8_t s_mute[USB_CHANNEL_NUM + 1];
static int16_t s_vol[USB_CHANNEL_NUM + 1];

// ---------------------------------------------------------------- device state
void tud_mount_cb(void)   { s_st.mounted = true;  ESP_LOGI(TAG, "USB mounted (UAC%d)", tud_audio_version()); }
void tud_umount_cb(void)  { s_st.mounted = false; s_st.streaming = false; ESP_LOGI(TAG, "USB unmounted"); }
void tud_suspend_cb(bool remote_wakeup_en) { (void)remote_wakeup_en; s_st.streaming = false; ESP_LOGI(TAG, "USB suspended"); }
void tud_resume_cb(void)  { ESP_LOGI(TAG, "USB resumed"); }

// ---------------------------------------------------------------- interface
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;
    uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
    uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));
    if (itf == ITF_NUM_AUDIO_STREAMING) {
        s_st.streaming = (alt != 0);
        s_st.alt_changes++;
        ESP_LOGI(TAG, "Streaming itf alt=%u (%s)", alt, alt ? "OPEN" : "closed");
    }
    return true;
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;
    uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
    uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));
    if (itf == ITF_NUM_AUDIO_STREAMING && alt == 0) s_st.streaming = false;
    return true;
}

// ---------------------------------------------------------------- endpoint controls (sampling freq)
bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff)
{
    (void)rhport;
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    if (ctrlSel == AUDIO10_EP_CTRL_SAMPLING_FREQ && p_request->bRequest == AUDIO10_CS_REQ_SET_CUR) {
        TU_VERIFY(p_request->wLength == 3);
        s_st.sample_rate = tu_unaligned_read32(pBuff) & 0x00FFFFFF;
        ESP_LOGI(TAG, "Host set sample rate %lu Hz", (unsigned long)s_st.sample_rate);
        return true;
    }
    return false;
}

bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    if (ctrlSel == AUDIO10_EP_CTRL_SAMPLING_FREQ && p_request->bRequest == AUDIO10_CS_REQ_GET_CUR) {
        uint32_t sr = s_st.sample_rate;
        uint8_t freq[3] = { (uint8_t)(sr & 0xFF), (uint8_t)((sr >> 8) & 0xFF), (uint8_t)((sr >> 16) & 0xFF) };
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, freq, sizeof(freq));
    }
    return false;
}

// ---------------------------------------------------------------- feature unit (mute / volume)
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff)
{
    (void)rhport;
    uint8_t ch = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    uint8_t entityID = TU_U16_HIGH(p_request->wIndex);
    if (entityID != UAC1_ENTITY_FEATURE_UNIT || ch > USB_CHANNEL_NUM) return false;
    if (p_request->bRequest != AUDIO10_CS_REQ_SET_CUR) return false;

    if (ctrlSel == AUDIO10_FU_CTRL_MUTE) {
        TU_VERIFY(p_request->wLength == 1);
        s_mute[ch] = pBuff[0];
        if (ch == 0) s_st.mute = pBuff[0];
        ESP_LOGI(TAG, "Host set mute ch%u = %u", ch, pBuff[0]);
        return true;
    }
    if (ctrlSel == AUDIO10_FU_CTRL_VOLUME) {
        TU_VERIFY(p_request->wLength == 2);
        s_vol[ch] = (int16_t)tu_unaligned_read16(pBuff);
        if (ch == 0) s_st.volume_db256 = s_vol[ch];
        ESP_LOGI(TAG, "Host set volume ch%u = %d/256 dB", ch, s_vol[ch]);
        return true;
    }
    return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    uint8_t ch = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    uint8_t entityID = TU_U16_HIGH(p_request->wIndex);
    if (entityID != UAC1_ENTITY_FEATURE_UNIT || ch > USB_CHANNEL_NUM) return false;

    if (ctrlSel == AUDIO10_FU_CTRL_MUTE) {
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &s_mute[ch], 1);
    }
    if (ctrlSel == AUDIO10_FU_CTRL_VOLUME) {
        int16_t v;
        switch (p_request->bRequest) {
            case AUDIO10_CS_REQ_GET_CUR: v = s_vol[ch]; break;
            case AUDIO10_CS_REQ_GET_MIN: v = -90 * 256; break;   // -90 dB
            case AUDIO10_CS_REQ_GET_MAX: v = 0;          break;   // 0 dB
            case AUDIO10_CS_REQ_GET_RES: v = 256;        break;   // 1 dB
            default: return false;
        }
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &v, sizeof(v));
    }
    return false;
}

// ---------------------------------------------------------------- tasks
static void tud_task_fn(void *arg)
{
    (void)arg;
    for (;;) tud_task();   // blocks internally on the TinyUSB event queue
}

static void rx_task_fn(void *arg)
{
    (void)arg;
    static uint8_t buf[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX * 4];
    while (s_running) {
        uint16_t avail = tud_audio_available();
        if (!avail) { vTaskDelay(1); continue; }
        uint16_t n = tud_audio_read(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
        if (!n) continue;
        s_st.rx_bytes += n;
        if (usb_in_pcm_buffer && xRingbufferSend(usb_in_pcm_buffer, buf, n, 0) != pdTRUE) {
            s_st.dropped_bytes += n;
        }
    }
    s_rx_task = NULL;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------- public API
esp_err_t usb_in_init(void (*init_done_cb)(void))
{
    if (s_initialized) return ESP_OK;
    s_init_done_cb = init_done_cb;

    usb_in_pcm_buffer = xRingbufferCreate(USB_PCM_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    ESP_RETURN_ON_FALSE(usb_in_pcm_buffer, ESP_ERR_NO_MEM, TAG, "ringbuf alloc failed");

    usb_phy_config_t phy_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_OTG_MODE_DEVICE,
        .otg_speed = USB_PHY_SPEED_FULL,
    };
    ESP_RETURN_ON_ERROR(usb_new_phy(&phy_conf, &s_phy), TAG, "usb_new_phy failed");

    ESP_RETURN_ON_FALSE(tud_init(BOARD_TUD_RHPORT), ESP_FAIL, TAG, "tud_init failed");
    BaseType_t ok = xTaskCreatePinnedToCore(tud_task_fn, "tinyusb", 4096, NULL, 10, &s_tud_task, 1);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "tinyusb task failed");

    s_initialized = true;
    ESP_LOGI(TAG, "UAC1 speaker ready: %d Hz, %d ch, %d-bit, EP size %d",
             USB_SAMPLE_RATE, USB_CHANNEL_NUM, USB_BIT_DEPTH, (int)CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX);
    if (s_init_done_cb) s_init_done_cb();
    return ESP_OK;
}

esp_err_t usb_in_start(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    if (s_running) return ESP_OK;
    s_running = true;
    BaseType_t ok = xTaskCreatePinnedToCore(rx_task_fn, "uac_rx", 4096, NULL, 9, &s_rx_task, 1);
    if (ok != pdPASS) { s_running = false; return ESP_ERR_NO_MEM; }
    return ESP_OK;
}

esp_err_t usb_in_stop(void)
{
    s_running = false;
    for (int i = 0; i < 20 && s_rx_task; i++) vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

void usb_in_deinit(void)
{
    usb_in_stop();
    // TinyUSB/PHY stay up; deinit is not needed for this sink.
}

uint32_t usb_in_get_sample_rate(void) { return s_st.sample_rate; }
bool usb_in_is_connected(void) { return tud_mounted(); }

void usb_in_get_stats(usb_in_stats_t *out)
{
    if (!out) return;
    memcpy(out, (const void *)&s_st, sizeof(*out));
    out->mounted = tud_mounted();
}
