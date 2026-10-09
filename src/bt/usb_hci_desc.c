/* usb_hci_desc.c - see usb_hci_desc.h.
 *
 * Descriptor layout from the USB 2.0 spec, chapter 9: each descriptor
 * starts with bLength, bDescriptorType. Interface = type 4, endpoint = 5.
 * Endpoint bmAttributes bits 1..0: 2 = bulk, 3 = interrupt. */
#include "usb_hci_desc.h"

#include <stdio.h>
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

/* Two bulk OUT pipes (MediaTek 0e8d:3603 has 0x01 and 0x02): ACL goes on
 * the one numbered like the bulk IN (0x82 -> 0x02). On that controller 0x01
 * stalls every ACL frame; the other stays as the spare. */
void usbhci_pair_out_with_in(struct usbhci_iface *f)
{
    if (f->spare_out_ep && (f->spare_out_ep & 0x0F) == (f->in_ep & 0x0F) &&
        (f->out_ep & 0x0F) != (f->in_ep & 0x0F)) {
        uint8_t t = f->out_ep;
        f->out_ep = f->spare_out_ep;
        f->spare_out_ep = t;
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

int usbhci_describe(const uint8_t *d, int len, char *out, size_t cap)
{
    static const char *const xfer[] = { "ctrl", "iso", "bulk", "int" };
    size_t n = 0;
    int pos = 0, ifaces = 0;

#define PUT(...) do {                                              \
        if (n < cap) {                                                 \
            int w_ = snprintf(out + n, cap - n, __VA_ARGS__);          \
            if (w_ > 0) n += (size_t)w_;                               \
            if (n >= cap) n = cap - 1;                                 \
        }                                                              \
    } while (0)
    if (!cap) return 0;
    out[0] = 0;
    while (pos + 2 <= len) {
        int blen = d[pos];
        int type = d[pos + 1];

        if (blen < 2 || pos + blen > len)
            break;
        if (type == DT_INTERFACE && blen >= 9) {
            PUT("%sif%u.%u %02x/%02x/%02x%s", ifaces ? "; " : "", d[pos + 2], d[pos + 3],
                d[pos + 5], d[pos + 6], d[pos + 7],
                (d[pos + 5] == 0xE0 && d[pos + 6] == 0x01 && d[pos + 7] == 0x01) ? " (BT HCI)" : "");
            ifaces++;
        } else if (type == DT_ENDPOINT && blen >= 7) {
            PUT(" ep%02x %s/%u", d[pos + 2], xfer[d[pos + 3] & 3u],
                (unsigned)((d[pos + 4] | d[pos + 5] << 8) & 0x7FF));
        }
        pos += blen;
    }
    if (!ifaces) PUT("no interfaces");
#undef PUT
    return (int)n;
}
