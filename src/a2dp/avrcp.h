/* AVRCP 1.5 absolute volume, both roles, over one AVCTP control channel.
 *  Target (headset is CT): GET_CAPABILITIES, REGISTER_NOTIFICATION
 *    (VOLUME_CHANGED), SET_ABSOLUTE_VOLUME, PASS THROUGH volume keys.
 *  Controller (we are CT): REGISTER_NOTIFICATION(VOLUME_CHANGED) and
 *    SET_ABSOLUTE_VOLUME toward the headset.
 * Pure byte-in/byte-out so it is host testable. Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_AVRCP_H
#define HEARBRIDGE_AVRCP_H

typedef struct {
    int volume;            /* 0..127, current absolute volume */
    int notify_label;      /* label of the headset's pending VOLUME_CHANGED reg, -1 none */
    int our_label;         /* next label for our commands */
    int ct_registered;     /* headset accepted our VOLUME_CHANGED registration */
    int remote_abs;        /* headset showed absolute volume support */
    int changed;           /* volume changed by the headset (consumer clears) */
    int need_register;     /* re-register for VOLUME_CHANGED after a CHANGED */
    int sink_renders;      /* the headset applies the volume itself (it took our
                              SetAbsoluteVolume or answered our registration) */
    int battery;           /* AVRCP battery status 0..4 (AVRCP_BATT_*), -1 unknown */
    int batt_label;        /* label of our BATT_STATUS_CHANGED registration, -1 none */
    int batt_tried;        /* registration sent once (headset refused = stays unknown) */
    int need_batt;         /* send the battery registration (after volume worked) */
    int vol_from_headset;  /* bumped on every volume change the headset made */
    unsigned long rx_cmds, rx_rsps;
    unsigned long vol_reports; /* every volume report / key / SetAbsoluteVolume from the headset */
    int vol_refused;       /* headset refused (or does not implement) our VOLUME_CHANGED registration */
    long now_ms;           /* caller's clock, set before avrcp_input / avrcp_build_set_volume */
    long our_set_ms;       /* when we last sent SetAbsoluteVolume */
    int seek_vol;          /* next (0x4b) / previous (0x4c) keys step the volume too */
    int key_pending;       /* a key changed the volume: SetAbsoluteVolume still to send */
    int hold_vol;          /* level we last set, until the headset echoes it (-1 none) */
} avrcp_state;

/* Name of a PASS THROUGH operation id (play, pause, ...), "" unknown. */
const char *avrcp_key_name(int key);

/* AVRCP 1.6 battery status (InformBatteryStatusOfCT 0x18 and
 * EVENT_BATT_STATUS_CHANGED 0x06). There is no percentage in AVRCP. */
enum { AVRCP_BATT_NORMAL = 0, AVRCP_BATT_WARNING = 1, AVRCP_BATT_CRITICAL = 2,
       AVRCP_BATT_EXTERNAL = 3, AVRCP_BATT_FULL = 4 };
/* Page key for a status: "ok", "low", "critical", "charging", "full"; "" unknown. */
const char *avrcp_battery_key(int status);
/* Rough level for the bar (0..100) from a status, -1 unknown. */
int avrcp_battery_level(int status);
/* Our registration for EVENT_BATT_STATUS_CHANGED toward the headset. */
int avrcp_build_register_battery(avrcp_state *a, unsigned char *out, int max);

/* Volume reports this soon after our SetAbsoluteVolume are its echo, not
 * the headset being turned: they do not count as moved on the headset. */
#define AVRCP_ECHO_MS 1000
/* After our SetAbsoluteVolume, a report with another level is stale (a
 * re-query answered from before, a slow headset) for this long, or until
 * the headset reports the level we set. */
#define AVRCP_HOLD_MS 1500
/* Volume keys pressed quickly: at most one SetAbsoluteVolume per this,
 * always with the latest level. */
#define AVRCP_KEY_SEND_MS 150

void avrcp_init(avrcp_state *a, int volume);

/* 1 once the headset has registered for volume or reported an absolute
 * level. Independent of the L2CAP channel still being open this instant:
 * the page's volume number comes from this. */
int avrcp_reported(const avrcp_state *a);


/* Handle one AVCTP packet from the headset. Writes a reply into out
 * (returns its length, 0 = none). */
int avrcp_input(avrcp_state *a, const unsigned char *in, int len,
                unsigned char *out, int max);

/* Build our commands. Return length. */
int avrcp_build_register_volume(avrcp_state *a, unsigned char *out, int max);
int avrcp_build_set_volume(avrcp_state *a, int vol, unsigned char *out, int max);
/* A volume key moved the level and it is time to send it (rate limit):
 * 1 = call avrcp_build_set_volume(a, a->volume, ...) now. A headset that
 * does not take absolute volume gets nothing (the software gain follows
 * a->volume) and the pending flag is cleared. */
int avrcp_key_due(avrcp_state *a);
/* After a local volume change: CHANGED notification for the headset's
 * registration (0 if it has none). */
int avrcp_build_volume_changed(avrcp_state *a, unsigned char *out, int max);

#endif
