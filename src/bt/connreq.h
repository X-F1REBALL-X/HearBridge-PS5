/* What to do with an incoming ACL Connection Request. Every request is
 * answered at once (or handed to the state machine that takes it), so none
 * waits ~25 s at the controller and blocks our pages with 0x0b. Devices that
 * are not audio and not ours (pads, phones) are left to the system. Pure.
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_CONNREQ_H
#define HEARBRIDGE_CONNREQ_H

typedef struct {
    int is_av;            /* Class of Device major class Audio/Video */
    int saved;            /* one of our saved headsets */
    int forgotten;        /* forgotten by the user this run */
    int held;             /* the user disconnected it by hand */
    int is_target;        /* the device we are connecting / pairing right now */
    int streaming_other;  /* we stream to another headset */
    int busy_other;       /* connecting / pairing another device */
} hb_cr_in;

enum {
    HB_CR_LEAVE = 0,      /* not ours: the system answers it */
    HB_CR_TAKE,           /* accept it (the current screen does) */
    HB_CR_SWITCH,         /* newest wins: drop the streaming one, take this */
    HB_CR_REJECT_BUSY,    /* Reject 0x0D */
    HB_CR_REJECT_UNKNOWN  /* Reject 0x0F */
};

int hb_cod_is_av(unsigned cod);
int hb_connreq_decide(const hb_cr_in *in);
const char *hb_connreq_name(int d);

#endif
