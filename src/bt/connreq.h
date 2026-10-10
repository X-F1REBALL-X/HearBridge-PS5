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
    int bg_page;          /* ...but only a background page nobody pressed for */
    int just_dropped;     /* we disconnected it a moment ago (switch / Disconnect) */
} hb_cr_in;

enum {
    HB_CR_LEAVE = 0,      /* not ours: the system answers it */
    HB_CR_TAKE,           /* accept it (the current screen does) */
    HB_CR_SWITCH,         /* newest wins: drop the streaming one, take this */
    HB_CR_REJECT_BUSY,    /* Reject 0x0D */
    HB_CR_REJECT_UNKNOWN, /* Reject 0x0F */
    HB_CR_ACCEPT_DROP     /* accept, then disconnect it cleanly (0x13): a
                           * headset we just left calling back. Some (Xbox
                           * Wireless Headset) stop answering pages until
                           * power cycled after a busy rejection. */
};

/* A saved headset we disconnected: its callback within HB_DROPPED_MS is
 * accepted and closed cleanly; a page to it that times out within
 * HB_DROPPED_PAGE_MS listens for it, then asks for a power cycle. */
#define HB_DROPPED_MS      10000
#define HB_DROPPED_PAGE_MS 60000
typedef struct {
    unsigned char addr[6];
    long t;
    int on;
} hb_dropped;

void hb_dropped_note(hb_dropped *d, const unsigned char addr[6], long now);
/* 1 when addr was dropped less than within_ms ago. */
int  hb_dropped_recent(const hb_dropped *d, const unsigned char addr[6], long now, long within_ms);

int hb_cod_is_av(unsigned cod);
int hb_connreq_decide(const hb_cr_in *in);
const char *hb_connreq_name(int d);

#endif
