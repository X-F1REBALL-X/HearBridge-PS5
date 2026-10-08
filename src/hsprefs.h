/* Per-headset settings (codec, latency target, equalizer), one small text
 * file per Bluetooth address under /data/hearbridge/prefs/.
 * Parsing and formatting are pure so the host tests cover them. */
#ifndef HEARBRIDGE_HSPREFS_H
#define HEARBRIDGE_HSPREFS_H

#define HB_PREFS_DIR "/data/hearbridge/prefs"

enum { HB_CODEC_AUTO = 0, HB_CODEC_SBC = 1, HB_CODEC_SBC_HQ = 2, HB_CODEC_SBC_XQ = 3, HB_CODEC_N };

#define HB_EQ_BANDS   5
#define HB_EQ_MAX_DB 12

#define HB_LAT_MIN_MS      60
#define HB_LAT_MAX_MS    1000
#define HB_LAT_DEFAULT_MS 200

typedef struct {
    int codec;               /* HB_CODEC_* (what the user picked) */
    int auto_no_xq;          /* auto: SBC-XQ did not hold on this link, use SBC */
    int latency_ms;          /* media queue target */
    int eq_on;
    int eq_db[HB_EQ_BANDS];  /* -12..+12 dB per band */
    int gain_pct;            /* software gain, -1 = not in the file yet */
    int held_codec;          /* codec that actually held last time, 0 = none */
    int held_bp;             /* bitpool that held with it, 0 = none */
} hb_prefs;

void hb_prefs_default(hb_prefs *p);
/* A headset with no saved file: keep whatever else was copied off the page,
 * but the buffer target is the default (200 ms), not an old 1 s mode. */
void hb_prefs_new_headset(hb_prefs *p);
/* Parse "key=value" lines; unknown keys and bad values are ignored. */
void hb_prefs_parse(hb_prefs *p, const char *text);
int  hb_prefs_format(const hb_prefs *p, char *out, int max);
const char *hb_codec_key(int codec);        /* "auto", "sbc", "hq", "xq" */
int  hb_codec_from_key(const char *s);      /* -1 if unknown */
/* <dir>/<AABBCCDDEEFF>.txt from a wire-order (little-endian) address. */
void hb_prefs_path(const char *dir, const unsigned char addr[6], char *out, int max);
/* 1 if a file was read. */
int  hb_prefs_load(const char *dir, const unsigned char addr[6], hb_prefs *p);
int  hb_prefs_save(const char *dir, const unsigned char addr[6], const hb_prefs *p);

#endif
