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
    {
        char p[96];
        CHECK(hb_game_icon_path("PPSA01325", 0, p, sizeof p) && !strcmp(p, "/user/appmeta/PPSA01325/icon0.png"),
              "icon: appmeta under /user first");
        CHECK(hb_game_icon_path("PPSA01325", 1, p, sizeof p) && !strcmp(p, "/system_data/priv/appmeta/PPSA01325/icon0.png"),
              "icon: then /system_data/priv");
        CHECK(!hb_game_icon_path("PPSA01325", 2, p, sizeof p) && !p[0], "icon: two places only");
        CHECK(!hb_game_icon_path("../../etc", 0, p, sizeof p) && !hb_game_icon_path("PPSA0132", 0, p, sizeof p),
              "icon: only real title ids (no path tricks)");
    }
    /* per game and per headset */
    {
        static hb_games ps, pb;
        static char big[HB_GPROF_MAX * 200];
        static const unsigned char SONY[6] = { 0x58, 0x18, 0x62, 0x63, 0x3B, 0x7C }, XBOX[6] = { 0xD8, 0xE2, 0xDF, 0xF7, 0xD7, 0x44 };
        unsigned char hs[6] = { 1, 2, 3, 4, 5, 0 };
        int ex = -1, idx[HB_GAME_MAX + 2], k, db[5] = { 6, 0, 0, 0, -3 };
        hb_game a;
        memset(&ps, 0, sizeof ps);
        /* an older games.txt line (no headset) */
        hb_games_parse(&ps, "PPSA01325 eq=on eq_db=6,0,0,0,-3 gain=300 hs_vol=90 name=ASTRO BOT\n");
        CHECK(ps.n == 1 && !ps.g[0].has_hs && ps.g[0].night == -1, "older line: no headset, night not saved");
        CHECK(hb_games_pick(&ps, "PPSA01325", SONY, &ex) == 0 && ex == 0, "older profile is used for any headset (not its own)");
        CHECK(hb_games_migrate(&ps, SONY) == 1 && ps.g[0].has_hs && !memcmp(ps.g[0].hs, SONY, 6) &&
              hb_games_migrate(&ps, SONY) == 0, "migrate: the older profile becomes the saved headset's, once");
        CHECK(hb_games_pick(&ps, "PPSA01325", SONY, &ex) == 0 && ex == 1, "Sony: its own profile");
        CHECK(hb_games_pick(&ps, "PPSA01325", XBOX, &ex) == 0 && ex == 0, "Xbox: falls back to the Sony one");
        CHECK(ps.n == 1, "fallback does not copy or overwrite anything");
        a = ps.g[0];
        a.gain_pct = 150; a.night = 1; memcpy(a.hs, XBOX, 6);
        hb_games_put(&ps, &a);
        CHECK(ps.n == 2 && hb_games_find_hs(&ps, "PPSA01325", XBOX) == 0 && hb_games_find_hs(&ps, "PPSA01325", SONY) == 1 &&
              ps.g[1].gain_pct == 300, "Update on the Xbox saves its own, the Sony one stays");
        CHECK(hb_games_pick(&ps, "PPSA01325", XBOX, &ex) == 0 && ex == 1 && hb_games_pick(&ps, "PPSA01325", SONY, &ex) == 1 && ex == 1,
              "each headset gets its own");
        CHECK(hb_games_list(&ps, idx, HB_GAME_MAX) == 1 && idx[0] == 0, "one game in the list (two headsets)");
        CHECK(!hb_game_differs(&ps.g[1], 1, db, 300, 0) && !hb_game_differs(&ps.g[1], 1, db, 300, 1),
              "same sound: no change (night not saved on the older one)");
        CHECK(hb_game_differs(&ps.g[0], 1, db, 150, 0), "night mode off differs");
        CHECK(!hb_game_differs(&ps.g[0], 1, db, 150, 1), "same sound with night: no change");
        db[2] = 1;
        CHECK(hb_game_differs(&ps.g[0], 1, db, 150, 1), "an EQ band differs");
        db[2] = 0;
        CHECK(hb_game_differs(&ps.g[0], 0, db, 150, 1) && hb_game_differs(&ps.g[0], 1, db, 151, 1),
              "EQ off or a boost change differs");
        CHECK(hb_games_format(&ps, big, sizeof big) > 0, "format per headset");
        CHECK(strstr(big, "PPSA01325 hs=D8:E2:DF:F7:D7:44 ") && strstr(big, " night=1 ") && strstr(big, "hs=58:18:62:63:3B:7C"),
              "games.txt carries the headset and night");
        memset(&pb, 0, sizeof pb);
        hb_games_parse(&pb, big);
        CHECK(pb.n == 2 && pb.g[0].has_hs && !memcmp(pb.g[0].hs, XBOX, 6) && pb.g[0].night == 1 && pb.g[0].gain_pct == 150 &&
              !memcmp(pb.g[1].hs, SONY, 6) && !strcmp(pb.g[1].name, "ASTRO BOT"), "per headset round trip");
        CHECK(hb_games_drop_hs(&ps, "PPSA01325", XBOX) && ps.n == 1 && !memcmp(ps.g[0].hs, SONY, 6) &&
              !hb_games_drop_hs(&ps, "PPSA01325", XBOX), "delete one headset's profile only");
        /* limits: 4 headsets per game, 32 games */
        for (k = 0; k < 6; k++) { hs[5] = (unsigned char)k; memcpy(a.hs, hs, 6); hb_games_put(&ps, &a); }
        CHECK(ps.n == HB_GAME_HS_MAX && hb_games_find_hs(&ps, "PPSA01325", SONY) < 0, "at most 4 headsets per game (oldest goes)");
        for (k = 0; k < HB_GAME_MAX + 3; k++) {
            snprintf(a.id, sizeof a.id, "CUSA%05d", k);
            hs[5] = 0; memcpy(a.hs, hs, 6); hb_games_put(&ps, &a);
            hs[5] = 1; memcpy(a.hs, hs, 6); hb_games_put(&ps, &a);
        }
        CHECK(hb_games_list(&ps, NULL, 0) == HB_GAME_MAX && ps.n == HB_GAME_MAX * 2 && hb_games_find(&ps, "PPSA01325") < 0 &&
              hb_games_find(&ps, "CUSA00002") < 0 && hb_games_find(&ps, "CUSA00003") >= 0,
              "at most 32 games (the least recently saved goes with all its headsets)");
        CHECK(hb_games_drop(&ps, "CUSA00010") == 2 && hb_games_find(&ps, "CUSA00010") < 0, "Remove a game: all its headsets");
        /* the migration does not replace a headset's own profile */
        memset(&ps, 0, sizeof ps);
        hb_games_parse(&ps, "PPSA01325 hs=58:18:62:63:3B:7C eq=off eq_db=0,0,0,0,0 gain=250 hs_vol=-1\nPPSA01325 eq=on eq_db=1,1,1,1,1 gain=200 hs_vol=-1\n");
        CHECK(ps.n == 2 && hb_games_migrate(&ps, SONY) == 1 && ps.n == 1 && !ps.g[0].eq_on, "migrate keeps the headset's own profile");
        CHECK(hb_games_find_hs(&ps, "PPSA01325", NULL) < 0, "nothing without a headset left");
    }
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
