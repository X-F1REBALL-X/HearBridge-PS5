/* hci.h - abstract HCI transport used by the host-side code.
 *
 * A transport moves three packet types defined by the Core spec, Vol 4
 * Part E 5.4: commands (host->controller), events (controller->host) and
 * ACL data (both ways). The console implementation is hci_usb.c; a test
 * harness can supply its own table of callbacks. */
#ifndef HEARBRIDGE_HCI_H
#define HEARBRIDGE_HCI_H

/* Largest packet a transport buffers (ACL header + payload). */
#define HCI_PKT_MAX 1100

typedef struct hci_ops {
    /* Read back queued input; byte count returned, 0 if the queue is empty.
     * Events are laid out as code, length, parameters; ACL packets keep
     * their 4-byte header. */
    int  (*next_acl)(void *self, unsigned char *dst, int cap);
    int  (*next_event)(void *self, unsigned char *dst, int cap);

    /* Drive I/O for up to `wait_ms`: 1 = input available, 0 = none,
     * -1 = transport gone (device removed, e.g. rest mode). */
    int  (*pump)(void *self, int wait_ms);

    /* Output. Both return 0 when the transport rejected the packet. */
    int  (*cmd)(void *self, unsigned op, const void *args, int nargs);
    int  (*acl_send)(void *self, const unsigned char *frame, int nbytes);

    /* Optional (NULL allowed): log transport state for troubleshooting. */
    void (*diag)(void *self);
    void (*close)(void *self);
} hci_ops;

/* A transport instance: callback table plus its private state. */
typedef struct {
    void          *ctx;
    const hci_ops *ops;
} hci_t;

#endif
