/* Developed by X-F1REBALL-X. */
#include "game_sys.h"
#include "gameprof.h"
#include "diag.h"
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
    uint32_t h = 0;
    if (g_init) return;
    g_init = 1;
    if (kernel_dynlib_handle(-1, "libSceSystemService.sprx", &h) != 0 || !h) {
        diag_set("game detection", "off: libSceSystemService.sprx not found");
        return;
    }
    g_appid = (fn_appid)kernel_dynlib_resolve(-1, h, NID_SS_RunningBigApp);
    if (!g_appid) g_appid = (fn_appid)kernel_dynlib_resolve(-1, h, NID_LNC_RunningBigApp);
    g_title[0] = (fn_title)kernel_dynlib_resolve(-1, h, NID_LNC_TitleId);
    g_title[1] = (fn_title)kernel_dynlib_resolve(-1, h, NID_SS_TitleId);
    g_ok = g_appid && (g_title[0] || g_title[1]);
    diag_set("game detection", "%s (running app %s, title id %s%s)", g_ok ? "on" : "off",
             g_appid ? "ok" : "missing", g_title[0] ? "lnc" : "", g_title[1] ? " ss" : "");
    log_line("game: detection %s", g_ok ? "on" : "not available on this firmware");
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
