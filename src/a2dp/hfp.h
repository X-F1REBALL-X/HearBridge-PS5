/* hfp.h - HFP Audio Gateway, battery only (Hands-Free Profile 1.8).
 *
 * The headset opens RFCOMM (L2CAP PSM 0x0003) to our HFP AG record and
 * sets up the Service Level Connection with AT commands. We answer just
 * enough for the SLC (BRSF, CIND, CMER, CHLD, BIND) and read the battery
 * the headset reports: AT+BIEV=2,<0..100> (HF indicator 2, exact percent)
 * or the Apple AT+IPHONEACCEV battery key (0..9, steps of 10 %). Never
 * any call or audio: ATA / ATD / AT+BCC are refused, no codec negotiation
 * is offered and SCO/eSCO requests are rejected by btlink.
 *
 * RFCOMM (TS 07.10 subset as in the RFCOMM 1.2 spec): responder side, the
 * multiplexer on DLCI 0, one data DLCI, credit based flow control. Pure:
 * frames go out through the send callback. Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_HFP_H
#define HEARBRIDGE_HFP_H

#define HFP_RFCOMM_CHANNEL 1          /* server channel in our SDP record */
#define HFP_AG_FEATURES    0x0400     /* +BRSF: HF indicators only (bit 10) */

/* RFCOMM frame types (control field without P/F). */
#define RFC_SABM 0x2F
#define RFC_UA   0x63
#define RFC_DM   0x0F
#define RFC_DISC 0x43
#define RFC_UIH  0xEF
#define RFC_PF   0x10

typedef void (*hfp_send_fn)(void *ud, const unsigned char *p, int n);
typedef void (*hfp_log_fn)(const char *msg);

typedef struct {
    int dlci, cr, type, pf;       /* address / control */
    int credits;                  /* UIH with P/F on a data DLCI: credit byte, else -1 */
    const unsigned char *info;
    int len;
} rfc_frame;

typedef struct {
    hfp_send_fn send;
    void *ud;
    hfp_log_fn log;
    int mux_up;                   /* DLCI 0 open */
    int peer_init;                /* the headset is the initiator (normal case) */
    int dlci;                     /* data DLCI, 0 = none */
    int dlci_up;
    int cfc;                      /* credit based flow control agreed */
    int tx_credits;               /* frames we may still send */
    int rx_credits;               /* frames the headset may still send */
    int mtu;                      /* RFCOMM max frame size (info bytes) */
    unsigned char outq[512];      /* AT replies waiting for credits */
    int outq_n;
    char line[160];               /* AT command being received */
    int line_n;
    int hf_features;              /* from AT+BRSF */
    int slc;                      /* Service Level Connection established */
    int bind_batt;                /* headset supports HF indicator 2 */
    int battery;                  /* 0..100, -1 unknown */
    int battery_seq;              /* bumped on every new value */
    int battery_src;              /* 0 none, 1 BIEV, 2 IPHONEACCEV */
    int closing;                  /* our close: 1 DISC on the DLC sent, 2 DISC on DLCI 0 sent */
} hfp_state;

void hfp_init(hfp_state *h, hfp_send_fn send, void *ud, hfp_log_fn log);
/* One L2CAP SDU from the RFCOMM channel (one RFCOMM frame). */
void hfp_input(hfp_state *h, const unsigned char *d, int len);

/* Clean close from our side (before the L2CAP channel goes): DISC on the
 * hands-free DLC, its UA, then DISC on DLCI 0. 1 = a close was started,
 * 0 = nothing open. Feed the answers through hfp_input. */
int  hfp_close(hfp_state *h);
/* 1 once nothing is open (or our close finished). */
int  hfp_closed(const hfp_state *h);

/* RFCOMM helpers (exposed for the tests). */
unsigned char rfc_fcs(const unsigned char *p, int n);
/* Build a frame; credits < 0 for none. Returns its length (0 if no room). */
int  rfc_build(unsigned char *out, int max, int dlci, int cr, int type, int pf,
               int credits, const unsigned char *info, int len);
/* Parse and check the FCS. 0 ok, -1 bad. cfc: credit flow is on. */
int  rfc_parse(const unsigned char *d, int len, int cfc, rfc_frame *f);

/* One AT command (without the trailing CR): writes the reply text
 * ("\r\n...\r\n\r\nOK\r\n") into out; returns its length. */
int  hfp_at(hfp_state *h, const char *cmd, char *out, int max);

#endif
