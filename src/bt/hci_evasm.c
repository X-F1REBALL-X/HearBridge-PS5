/* hci_evasm.c - see hci_evasm.h. */
#include "hci_evasm.h"

#include <string.h>

void hci_evasm_reset(struct hci_evasm *a)
{
    memset(a, 0, sizeof *a);
}

void hci_evasm_push(struct hci_evasm *a, const uint8_t *chunk, int n, long t,
                    hci_evasm_cb cb, void *user)
{
    int room, need, rest;

    if (a->fill > 0 && t - a->last_ms > HCI_EVASM_STALE_MS) {
        a->abandoned++;
        a->fill = 0;
    }
    a->last_ms = t;
    while (n > 0) {
        room = (int)sizeof a->acc - a->fill;
        if (n > room)
            n = room;               /* cannot happen with sane chunks */
        memcpy(a->acc + a->fill, chunk, (size_t)n);
        a->fill += n;
        chunk += n;
        n = 0;

        /* Emit every complete event now sitting at the front. */
        while (a->fill >= 2) {
            need = 2 + a->acc[1];
            if (a->fill < need)
                break;
            cb(user, a->acc, need);
            rest = a->fill - need;
            memmove(a->acc, a->acc + need, (size_t)rest);
            a->fill = rest;
        }
    }
}
