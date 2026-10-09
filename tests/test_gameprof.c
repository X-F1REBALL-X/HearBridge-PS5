/* Developed by X-F1REBALL-X. Per-game audio profiles. */
#include "gameprof.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

int main(void)
{
    hb_games gs, back;
    hb_game g;
    char buf[8192], t[HB_GAME_NAME];
    int i;
    CHECK(hb_game_id_ok("PPSA01325") && hb_game_id_ok("CUSA00001"), "title ids accepted");
    CHECK(!hb_game_id_ok("ppsa01325") && !hb_game_id_ok("PPSA0132") && !hb_game_id_ok("PPSA013250") &&
          !hb_game_id_ok("../etc/x") && !hb_game_id_ok(NULL), "bad ids refused");
    memset(&gs, 0, sizeof gs);
    memset(&g, 0, sizeof g);
    strcpy(g.id, "PPSA01325");
    strcpy(g.name, "ASTRO BOT");
    g.eq_on = 1; g.eq_db[0] = 6; g.eq_db[4] = -3; g.gain_pct = 300; g.hs_vol = 90;
    hb_games_put(&gs, &g);
    CHECK(gs.n == 1 && hb_games_find(&gs, "PPSA01325") == 0 && hb_games_find(&gs, "CUSA00001") < 0, "put / find");
    g.gain_pct = 200;
    hb_games_put(&gs, &g);
    CHECK(gs.n == 1 && gs.g[0].gain_pct == 200, "put again replaces");
    CHECK(hb_games_format(&gs, buf, sizeof buf) > 0, "format");
    memset(&back, 0, sizeof back);
    hb_games_parse(&back, buf);
    CHECK(back.n == 1 && !strcmp(back.g[0].name, "ASTRO BOT") && back.g[0].eq_on && back.g[0].eq_db[0] == 6 &&
          back.g[0].eq_db[4] == -3 && back.g[0].gain_pct == 200 && back.g[0].hs_vol == 90, "format -> parse round trip");
    for (i = 0; i < HB_GAME_MAX + 5; i++) {
        snprintf(g.id, sizeof g.id, "CUSA%05d", i);
        hb_games_put(&gs, &g);
    }
    CHECK(gs.n == HB_GAME_MAX, "never more than the max");
    CHECK(hb_games_drop(&gs, "CUSA00010") && hb_games_find(&gs, "CUSA00010") < 0 && !hb_games_drop(&gs, "CUSA00010"),
          "drop");
    CHECK(hb_game_decide("", "PPSA01325", 1) == HB_GAME_APPLY, "game with a profile starts: apply");
    CHECK(hb_game_decide("PPSA01325", "PPSA01325", 1) == HB_GAME_KEEP, "same game: keep");
    CHECK(hb_game_decide("PPSA01325", "", 0) == HB_GAME_RESTORE, "game quit: headset settings back");
    CHECK(hb_game_decide("PPSA01325", "CUSA00001", 0) == HB_GAME_RESTORE, "other game without a profile: restore");
    CHECK(hb_game_decide("", "CUSA00001", 0) == HB_GAME_KEEP, "no profile, none applied: keep");
    CHECK(hb_game_json_title("{\"localizedParameters\":{\"en-US\":{\"titleName\":\"ASTRO BOT\"}}}", t, sizeof t) &&
          !strcmp(t, "ASTRO BOT"), "title name from param.json");
    CHECK(!hb_game_json_title("{\"x\":1}", t, sizeof t), "no title name");
    {
        char js[200], small[8];
        snprintf(js, sizeof js, "{\"titleName\":\"ab\xc3\xa9\xc3\xa9\xc3\xa9\"}");
        hb_game_json_title(js, small, sizeof small);
        CHECK(strlen(small) < sizeof small && ((unsigned char)small[strlen(small) - 1] & 0xC0) != 0xC0,
              "long title cut on a character boundary");
    }
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
