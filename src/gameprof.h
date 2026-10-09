/* Per-game audio profile: EQ, boost and headset volume saved for one game
 * (title id like PPSA01234), applied while that game runs and dropped back
 * to the headset's own settings when it closes. One text file,
 * /data/hearbridge/games.txt, one line per game. Pure (no system calls),
 * the console side is game_sys.c. Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_GAMEPROF_H
#define HEARBRIDGE_GAMEPROF_H

#define HB_GAMES_PATH "/data/hearbridge/games.txt"
#define HB_GAME_MAX   32
#define HB_GAME_NAME  48

typedef struct {
    char id[16];             /* PPSA01234 / CUSA01234 */
    char name[HB_GAME_NAME]; /* shown on the page, may be empty */
    int eq_on;
    int eq_db[5];
    int gain_pct;            /* 0..500 */
    int hs_vol;              /* 0..127, -1 = leave the headset volume alone */
} hb_game;

typedef struct {
    hb_game g[HB_GAME_MAX];
    int n;
} hb_games;

/* 1 for four capital letters + five digits (PPSA01234, CUSA00001, ...). */
int  hb_game_id_ok(const char *id);
void hb_games_parse(hb_games *gs, const char *text);
int  hb_games_format(const hb_games *gs, char *out, int max);
/* Index of id, -1 if none. */
int  hb_games_find(const hb_games *gs, const char *id);
/* Add or replace (newest first; the oldest falls off when full). */
void hb_games_put(hb_games *gs, const hb_game *g);
/* 1 if it was there. */
int  hb_games_drop(hb_games *gs, const char *id);

/* What to do when the running game changes. applied = id whose profile is
 * on now ("" none), cur = game running now ("" none). */
enum { HB_GAME_KEEP = 0, HB_GAME_APPLY = 1, HB_GAME_RESTORE = 2 };
int  hb_game_decide(const char *applied, const char *cur, int cur_has_profile);

/* "titleName" from a param.json (first one found), UTF-8 kept, escapes
 * \" \\ \/ handled, others dropped. 1 if found. */
int  hb_game_json_title(const char *json, char *out, int max);

#endif
