/* HearBridge PS5 — A2DP pair spike (pair-only diagnostic).
 *
 * Inquiry → audio-class candidates (CoD Audio/Video major or Audio/
 * Rendering service) → Create Connection + SSP auth/encrypt → write
 * /data/hearbridge/headset.ini → disconnect. Any A2DP-capable device.
 *
 * CONFLICT: stop other Bluetooth payloads first — both own Function 0.
 * Headphones: pairing / discoverable mode.
 * Developed by X-F1REBALL-X.
 */
#include "a2dp/a2dp.h"
#include "hci_cmd.h"
#include "hci_usb.h"
#include "lock.h"
#include "log.h"
#include "notify.h"
#include "util.h"
#include "version.h"

#include <sys/stat.h>

#include <stdio.h>
#include <string.h>

#define STATE_DIR "/data/hearbridge"
#define LOG_PATH  STATE_DIR "/hearbridge.log"
#define LOCK_PATH STATE_DIR "/hearbridge.lock"

static const char g_version_tag[] __attribute__((used)) =
    "hearbridge-version " HEARBRIDGE_VERSION;
static const char g_author_tag[] __attribute__((used)) =
    "hearbridge-author X-F1REBALL-X";

int main(void)
{
    hci_t hci;
    a2dp_session *asess = NULL;
    a2dp_open_opts opts;
    a2dp_inq_dev found[A2DP_INQ_MAX];
    a2dp_pair_result pr;
    int nfound = 0, pick, rc = 2;
    char astr[18];

    mkdir(STATE_DIR, 0755);

    if (!lock_take(LOCK_PATH)) {
        notify("HearBridge: already running");
        return 1;
    }

    log_open(LOG_PATH);
    log_line("HearBridge PS5 %s — A2DP pair spike", HEARBRIDGE_VERSION);
    log_line("Stop other Bluetooth payloads before this ELF (shared Function 0)");
    log_line("Put headphones/speaker in pairing / discoverable mode");
    notify("HearBridge %s\npairing…", HEARBRIDGE_VERSION);

    memset(&hci, 0, sizeof hci);
    if (!hci_usb_open(&hci)) {
        log_line("spike FAIL — hci_usb_open");
        notify("HearBridge: HCI open failed\nAnother Bluetooth payload running?");
        goto out;
    }

    memset(&opts, 0, sizeof opts);
    opts.inquiry_seconds = 12;
    asess = a2dp_open(hci, &opts);
    if (!asess) {
        log_line("spike FAIL — a2dp_open");
        notify("HearBridge: controller setup failed");
        if (hci.ops && hci.ops->close) hci.ops->close(hci.ctx);
        goto out;
    }

    memset(found, 0, sizeof found);
    if (!a2dp_inquiry(asess, found, A2DP_INQ_MAX, &nfound)) {
        log_line("spike FAIL — inquiry");
        notify("HearBridge: pair FAIL\ninquiry");
        goto cleanup;
    }

    {
        int order[A2DP_INQ_MAX], ncand, i, ok = 0;
        ncand = a2dp_rank_sinks(found, nfound, NULL, order, A2DP_INQ_MAX);
        for (i = 0; i < ncand && !ok; i++) {
            pick = order[i];
            log_line("spike: pairing candidate %d/%d CoD %06x \"%s\"", i + 1,
                     ncand, (unsigned)found[pick].cod,
                     found[pick].name[0] ? found[pick].name : "-");
            memset(&pr, 0, sizeof pr);
            ok = a2dp_pair(asess, &found[pick], &pr);
        }
        if (!ok) {
            log_line("spike FAIL — no audio device paired");
            notify("HearBridge: pair FAIL\npairing mode?");
            goto cleanup;
        }
    }

    hci_addr_str(pr.addr, astr);
    log_line("spike PASS — paired %s \"%s\" handle %#05x key_type=%u",
             astr, pr.name[0] ? pr.name : "audio device",
             pr.handle >= 0 ? pr.handle : 0, pr.key_type);
    notify("HearBridge: pair PASS\n%s",
           pr.name[0] ? pr.name : "audio device");
    rc = 0;

cleanup:
    if (asess) a2dp_close(asess);
    if (hci.ops && hci.ops->close) hci.ops->close(hci.ctx);
out:
    log_close();
    lock_release();
    return rc;
}
