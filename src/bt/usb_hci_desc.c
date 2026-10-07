/* usb_hci_desc.c - see usb_hci_desc.h.
 *
 * Descriptor layout from the USB 2.0 spec, chapter 9: each descriptor
 * starts with bLength, bDescriptorType. Interface = type 4, endpoint = 5.
 * Endpoint bmAttributes bits 1..0: 2 = bulk, 3 = interrupt. */
#include "usb_hci_desc.h"

#include <string.h>

enum { DT_INTERFACE = 4, DT_ENDPOINT = 5 };
enum { XFER_BULK = 2, XFER_INTR = 3 };

static int complete(const struct usbhci_iface *f)
{
    return f->evt_ep && f->in_ep && f->out_ep;
}

static void add_endpoint(struct usbhci_iface *f, const uint8_t *e)
{
    uint8_t addr = e[2];
    unsigned type = e[3] & 3u;
    uint16_t mps = (uint16_t)((e[4] | e[5] << 8) & 0x7FF);
    int is_in = (addr & 0x80) != 0;

    if (type == XFER_INTR && is_in && !f->evt_ep) {
        f->evt_ep = addr;
        f->evt_mps = mps;
    } else if (type == XFER_BULK && is_in && !f->in_ep) {
        f->in_ep = addr;
        f->in_mps = mps;
    } else if (type == XFER_BULK && !is_in) {
        if (!f->out_ep) {
            f->out_ep = addr;
            f->out_mps = mps;
        } else if (!f->spare_out_ep) {
            f->spare_out_ep = addr;
        }
    }
}

int usbhci_scan(const uint8_t *d, int len, struct usbhci_iface *found)
{
    struct usbhci_iface cur;
    int pos = 0, count = 0, inside = 0;

    memset(&cur, 0, sizeof cur);
    while (pos + 2 <= len) {
        int blen = d[pos];
        int type = d[pos + 1];

        if (blen < 2 || pos + blen > len)
            break;
        if (type == DT_INTERFACE && blen >= 9) {
            if (inside && complete(&cur) && count < USBHCI_MAX_IFACES)
                found[count++] = cur;
            memset(&cur, 0, sizeof cur);
            /* bAlternateSetting == 0, class/subclass/protocol E0/01/01 */
            inside = d[pos + 3] == 0 && d[pos + 5] == 0xE0 &&
                     d[pos + 6] == 0x01 && d[pos + 7] == 0x01;
            cur.number = d[pos + 2];
        } else if (type == DT_ENDPOINT && blen >= 7 && inside) {
            add_endpoint(&cur, d + pos);
        }
        pos += blen;
    }
    if (inside && complete(&cur) && count < USBHCI_MAX_IFACES)
        found[count++] = cur;
    return count;
}
