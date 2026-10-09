/* Shared state between the streaming loop and the web control thread.
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_CTL_H
#define HEARBRIDGE_CTL_H

#include <pthread.h>

#define HB_GAIN_DEFAULT_PCT 250   /* software base gain, middle of the slider */
#define HB_GAIN_MAX_PCT     500

/* Per-run random token: the page carries it and sends it back in the
 * X-HB-Token header of every state-changing POST (see http.c). */
#define HB_TOKEN_LEN 32

typedef struct {
    pthread_mutex_t mu;
    /* controls (web → stream loop) */
    int gain_pct;          /* 0..500: software base gain */
    int gain_dirty;        /* persist gain_pct to the gain file */
    int muted;
    int tone;              /* web tone toggle (or the tone file) */
    int req_hs_volume;     /* 0..127 to send as SetAbsoluteVolume, -1 none */
    int req_connect, req_disconnect, req_stop, req_reset;
    int paused;            /* user pressed Disconnect: stay idle */
    int latency_ms;        /* media queue target, 60..1000 ms (per headset) */
    int codec_pref;        /* HB_CODEC_* picked on the page (per headset) */
    int prefs_dirty;       /* per-headset settings changed: save them */
    int codec_avail;       /* bit per HB_CODEC_* the current sink takes (0 = unknown) */
    int eq_on;             /* equalizer (per headset) */
    int eq_db[5];          /* -12..12 dB: 80 Hz shelf, 250, 1k, 3.5k, 10 kHz shelf */
    unsigned eq_seq;       /* bumped on every change (stream loop re-designs) */
    int night;             /* night mode compressor (per headset) */
    int night_db10;        /* its gain right now, tenths of a dB (status) */
    int batt_alert;        /* last low battery heads-up: 20 / 10, 0 none */
    unsigned batt_alert_seq; /* bumped with each one (the page toasts once) */
    int rest_watch;        /* 1 = rest mode requests can be seen */
    /* status (stream loop → web) */
    char token[HB_TOKEN_LEN + 1];  /* hex, set once by ctl_init() */
    char version[16];
    char state[32];        /* idle / connecting / streaming / reconnecting / paused */
    char device[64];
    char url[64];
    char detail[96];       /* full status line (e.g. "waiting-selection 3") */
    char why[16];          /* short disconnect reason key, "" if none */
    char devices_path[96]; /* devices.json written by the scan */
    int lat_total, lat_capture, lat_packet, lat_queue, lat_radio, lat_sink; /* estimate, ms */
    int lat_sink_reported; /* lat_sink from an AVDTP delay report */
    char select_path[96];  /* select.txt read by the chooser */
    char saved_path[96];   /* saved.json: paired headsets (no keys) */
    int hs_volume;         /* 0..127, -1 unknown */
    int avrcp;             /* btlink_avrcp_state() bits */
    long pkts, frames, empty_reads, uptime_s;
    int peak_milli;        /* capture peak x1000 (last second) */
    int out_peak_milli;    /* after gain/limiter */
    char codec[24];        /* e.g. "SBC" / "SBC-XQ" (empty = none) */
    int sample_rate, bitpool, backlog;
    int per_packet;        /* SBC frames per media packet */
    int bitpool_lo, bitpool_hi; /* adaptive range */
    long dropped;          /* media packets dropped (radio too slow) */
    int xq_low;            /* SBC-XQ held under HB_XQ_LOW_BP: tell the page */
    int chip_vid, chip_pid; /* Bluetooth controller USB IDs, -1 until known */
#define HB_EVENT_N   14
#define HB_EVENT_LEN 80
    char events[HB_EVENT_N][HB_EVENT_LEN]; /* last codec switches / disconnects */
    int event_n;
    unsigned cmd_seq;      /* bumped by the web thread for each select.txt command */
    /* headset extras (stream loop -> web) */
    int battery;           /* AVRCP battery status 0..4, -1 unknown */
    int hs_moves;          /* volume changes made on the headset itself */
    int link_rssi, link_lq; /* HCI Read RSSI (127 unknown) / Link Quality (-1 unknown) */
    int link_score;        /* 0..100, -1 unknown (linkq.h) */
    int drops_min;         /* media packets dropped per minute (smoothed) */
    /* running game + its profile */
    int game_avail;        /* game detection works on this firmware */
    char game_id[16];      /* "" no game */
    char game_name[48];
    int game_profile;      /* a profile is saved for game_id */
    int game_active;       /* that profile is applied now */
    int req_game;          /* page: 1 save for this game, 2 forget it */
    int lat_backoff_ms;    /* low buffer target stepped back this much (drops) */
    int lat_normal_ms;     /* last delay estimate at the default 200 ms target, 0 none */
    /* backup / restore */
    int req_reload;        /* restore wrote new settings: reload them */
    char state_dir[64];    /* /data/hearbridge (tests point it elsewhere) */
    char mnt_root[32];     /* /mnt (USB drives at <mnt_root>/usbN) */
    long t0_s;             /* monotonic seconds at ctl_init (uptime base) */
} hb_ctl;

extern hb_ctl g_ctl;

void ctl_init(hb_ctl *c, const char *version);
/* New random c->token (128 bits as 32 hex digits): /dev/urandom, or a
 * clock/pid mix when it cannot be read. Called by ctl_init(). */
void ctl_new_token(hb_ctl *c);
/* Zero the per-link status (counters, format, AVRCP, meters); with
 * drop_device also forget the device name. Caller holds the lock. */
void ctl_clear_link(hb_ctl *c, int drop_device);
/* Remember one short line for the page (codec switch or disconnect).
 * Drops the oldest when the list is full. Locks itself. */
void ctl_event(hb_ctl *c, const char *line);
/* Same, caller already holds the lock. */
void ctl_event_locked(hb_ctl *c, const char *line);
/* Seconds since HearBridge started. */
long ctl_uptime_s(const hb_ctl *c);
#define CTL_LOCK(c)   pthread_mutex_lock(&(c)->mu)
#define CTL_UNLOCK(c) pthread_mutex_unlock(&(c)->mu)

/* Effective software gain (x1000) from base gain, mute and headset volume.
 * hs_volume < 0 (no absolute volume) → base gain only. */
int ctl_effective_gain_milli(int gain_pct, int muted, int hs_volume);

#endif
