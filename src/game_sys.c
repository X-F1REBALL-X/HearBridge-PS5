/* Developed by X-F1REBALL-X. */
#include "game_sys.h"
#include "gameprof.h"
#include "diag.h"
#include "dynmod.h"
#include "log.h"

#include <ps5/kernel.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NID_SS_RunningBigApp  "NwT2R5lo8P8"  /* sceSystemServiceGetAppIdOfRunningBigApp */
#define NID_LNC_RunningBigApp "SoQULIDd9V4"  /* sceLncUtilGetAppIdOfRunningBigApp */
#define NID_LNC_TitleId       "g0wTG9KImzI"  /* sceLncUtilGetAppTitleId */
#define NID_SS_TitleId        "0YmI09jOP-g"  /* sceSystemServiceGetAppTitleId */

typedef int (*fn_appid)(void);
typedef int (*fn_title)(uint32_t, char *);

static fn_appid g_appid;
static fn_title g_title[2];
static int g_init, g_ok;

static void init(void)
{
    char how[128];
    uint32_t h;
    if (g_init) return;
    g_init = 1;
    h = hb_mod_open("libSceSystemService.sprx", how, sizeof how);
    diag_set("libSceSystemService.sprx", "%s", how);
    if (!h) {
        diag_set("game detection", "off: libSceSystemService.sprx %s", how);
        log_line("game: detection off, libSceSystemService.sprx %s", how);
        return;
    }
    g_appid = (fn_appid)hb_mod_sym(h, NID_SS_RunningBigApp, "sceSystemServiceGetAppIdOfRunningBigApp");
    if (!g_appid) g_appid = (fn_appid)hb_mod_sym(h, NID_LNC_RunningBigApp, "sceLncUtilGetAppIdOfRunningBigApp");
    g_title[0] = (fn_title)hb_mod_sym(h, NID_LNC_TitleId, "sceLncUtilGetAppTitleId");
    g_title[1] = (fn_title)hb_mod_sym(h, NID_SS_TitleId, "sceSystemServiceGetAppTitleId");
    g_ok = g_appid && (g_title[0] || g_title[1]);
    diag_set("game detection", "%s (handle %#x; running app %p, title id lnc %p, ss %p)", g_ok ? "on" : "off",
             h, (void *)g_appid, (void *)g_title[0], (void *)g_title[1]);
    log_line("game: detection %s (handle %#x, running app %p, lnc title %p, ss title %p)",
             g_ok ? "on" : "not available on this firmware", h, (void *)g_appid,
             (void *)g_title[0], (void *)g_title[1]);
}

int hb_game_sys_avail(void)
{
    init();
    return g_ok;
}

int hb_game_sys_running(char *id, int max)
{
    int app, i;
    if (max > 0) id[0] = 0;
    init();
    if (!g_ok || max < 10) return 0;
    app = g_appid();
    if (app == -1 || app == 0) return 0;
    for (i = 0; i < 2; i++) {
        char t[32];
        if (!g_title[i]) continue;
        memset(t, 0, sizeof t);
        if (g_title[i]((uint32_t)app, t) == 0 && hb_game_id_ok(t)) {
            snprintf(id, (size_t)max, "%s", t);
            return 1;
        }
    }
    return 0;
}

void hb_game_sys_name(const char *id, char *name, int max)
{
    static char js[32768];
    char p[80];
    FILE *f;
    size_t n;
    if (max > 0) name[0] = 0;
    if (!hb_game_id_ok(id)) return;
    snprintf(p, sizeof p, "/user/appmeta/%s/param.json", id);
    f = fopen(p, "r");
    if (!f) {
        snprintf(p, sizeof p, "/system_data/priv/appmeta/%s/param.json", id);
        f = fopen(p, "r");
    }
    if (!f) return;
    n = fread(js, 1, sizeof js - 1, f);
    fclose(f);
    js[n] = 0;
    (void)hb_game_json_title(js, name, max);
}
