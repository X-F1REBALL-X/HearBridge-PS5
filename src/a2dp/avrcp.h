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
    unsigned long rx_cmds, rx_rsps;
} avrcp_state;

void avrcp_init(avrcp_state *a, int volume);

/* Handle one AVCTP packet from the headset. Writes a reply into out
 * (returns its length, 0 = none). */
int avrcp_input(avrcp_state *a, const unsigned char *in, int len,
                unsigned char *out, int max);

/* Build our commands. Return length. */
int avrcp_build_register_volume(avrcp_state *a, unsigned char *out, int max);
int avrcp_build_set_volume(avrcp_state *a, int vol, unsigned char *out, int max);
/* After a local volume change: CHANGED notification for the headset's
 * registration (0 if it has none). */
int avrcp_build_volume_changed(avrcp_state *a, unsigned char *out, int max);

#endif
