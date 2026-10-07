/* HearBridge PS5 — Avcap2 capture spike (v0.0.1-spike).
 *
 * Proves game/UI audio capture on FW 10.20 via libSceAvcap2 without opening
 * the Bluetooth HCI. Writes raw PCM and stats under /data/hearbridge/.
 *
 * Does NOT conflict with other Bluetooth payloads at the radio layer (HCI is untouched).
 * Still take the HearBridge lock so two HearBridge copies do not fight.
 *
 * Developed by X-F1REBALL-X.
 */
#include "avcap2.h"
#include "lock.h"
#include "log.h"
#include "notify.h"
#include "version.h"

#include <sys/stat.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define STATE_DIR   "/data/hearbridge"
#define LOG_PATH    STATE_DIR "/hearbridge.log"
#define LOCK_PATH   STATE_DIR "/hearbridge.lock"
#define CAPTURE_RAW STATE_DIR "/capture-test.raw"
#define STOP_FLAG   STATE_DIR "/stop"

#define CAPTURE_SECONDS 5

static const char g_version_tag[] __attribute__((used)) =
    "hearbridge-version " HEARBRIDGE_VERSION;
static const char g_author_tag[] __attribute__((used)) =
    "hearbridge-author X-F1REBALL-X";

int main(void)
{
    int rc;

    mkdir(STATE_DIR, 0755);

    if (!lock_take(LOCK_PATH)) {
        /* Log may not be open yet; still try a toast. */
        notify("HearBridge: already running");
        return 1;
    }

    if (!log_open(LOG_PATH)) {
        /* Continue with stderr-only logging via log_line fallback. */
    }

    log_line("HearBridge PS5 %s — Avcap2 spike (no HCI)", HEARBRIDGE_VERSION);
    log_line("data dir %s", STATE_DIR);
    notify("HearBridge %s\nAvcap2 spike — capturing…", HEARBRIDGE_VERSION);

    unlink(STOP_FLAG);
    rc = avcap2_capture_to_file(CAPTURE_RAW, CAPTURE_SECONDS);

    if (rc == 0) {
        log_line("spike PASS — see %s and this log", CAPTURE_RAW);
        notify("HearBridge: capture OK\nCheck /data/hearbridge/");
    } else {
        log_line("spike FAIL — see log for details");
        notify("HearBridge: capture failed\nSee hearbridge.log");
    }

    log_close();
    lock_release();
    return rc == 0 ? 0 : 2;
}
