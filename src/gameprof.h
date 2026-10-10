/* Per-game audio profile: EQ, boost and headset volume saved for one game
 * (title id like PPSA01234), applied while that game runs and dropped back
 * to the headset's own settings when it closes. One text file,
 * /data/hearbridge/games.txt, one line per game. Pure (no system calls),
 * the console side is game_sys.c. Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_GAMEPROF_H
#define HEARBRIDGE_GAMEPROF_H

#define HB_GAMES_PATH "/data/hearbridge/games.txt"
#define HB_GAME_MAX    32                          /* games */
#define HB_GAME_HS_MAX 4                           /* headsets per game */
#define HB_GPROF_MAX   (HB_GAME_MAX * HB_GAME_HS_MAX)
#define HB_GAME_NAME   48

/* One profile: a game on one headset ("hs=" in games.txt). Older files have
 * no headset (has_hs 0); those are moved to the current headset once one is
 * known (hb_games_migrate). */
typedef struct {
    char id[16];             /* PPSA01234 / CUSA01234 */
    char name[HB_GAME_NAME]; /* shown on the page, may be empty */
    int eq_on;
    int eq_db[5];
    int gain_pct;            /* 0..500 */
    int hs_vol;              /* 0..127, -1 = leave the headset volume alone */
    int night;               /* 0/1, -1 = not saved (older file): leave it */
    int has_hs;
    unsigned char hs[6];     /* headset address */
} hb_game;

typedef struct {
    hb_game g[HB_GPROF_MAX]; /* newest first */
    int n;
} hb_games;

/* 1 for four capital letters + five digits (PPSA01234, CUSA00001, ...). */
int  hb_game_id_ok(const char *id);
void hb_games_parse(hb_games *gs, const char *text);
int  hb_games_format(const hb_games *gs, char *out, int max);
/* Newest profile of id (any headset), -1 if none. */
int  hb_games_find(const hb_games *gs, const char *id);
/* The profile of id for headset hs exactly (hs NULL: the one without a
 * headset), -1 if none. */
int  hb_games_find_hs(const hb_games *gs, const char *id, const unsigned char *hs);
/* What a running game uses on headset hs: its own profile (*exact = 1), else
 * the game's newest one from another headset (*exact = 0), -1 if none. */
int  hb_games_pick(const hb_games *gs, const char *id, const unsigned char *hs, int *exact);
/* Add or replace the (id, headset) profile, newest first. At most
 * HB_GAME_HS_MAX headsets per game (the oldest goes) and HB_GAME_MAX games
 * (the least recently saved game goes, all its headsets). */
void hb_games_put(hb_games *gs, const hb_game *g);
/* Every profile of id; how many went. */
int  hb_games_drop(hb_games *gs, const char *id);
/* One headset's profile of id; 1 if it was there. */
int  hb_games_drop_hs(hb_games *gs, const char *id, const unsigned char *hs);
/* Profiles without a headset become hs's (one hs already has is dropped
 * instead). How many changed. */
int  hb_games_migrate(hb_games *gs, const unsigned char hs[6]);
/* Distinct games, newest first: idx[k] = index of game k's newest profile. */
int  hb_games_list(const hb_games *gs, int *idx, int max);
/* 1 if the sound now (EQ on/bands, boost, night) is not what g saved. */
int  hb_game_differs(const hb_game *g, int eq_on, const int eq_db[5], int gain_pct, int night);

/* What to do when the running game changes. applied = id whose profile is
 * on now ("" none), cur = game running now ("" none). */
enum { HB_GAME_KEEP = 0, HB_GAME_APPLY = 1, HB_GAME_RESTORE = 2 };
int  hb_game_decide(const char *applied, const char *cur, int cur_has_profile);

/* i-th place to look for the game's icon (0, 1, ...): appmeta icon0.png
 * under /user then /system_data/priv. 0 when i is past the last or id is
 * not a title id. */
int  hb_game_icon_path(const char *id, int i, char *out, int max);

/* "titleName" from a param.json (first one found), UTF-8 kept, escapes
 * \" \\ \/ handled, others dropped. 1 if found. */
int  hb_game_json_title(const char *json, char *out, int max);

#endif
