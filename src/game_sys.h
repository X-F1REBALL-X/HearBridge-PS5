/* Which game is running (read only: never launches, suspends or closes
 * anything). The calls are looked up at run time in libSceSystemService
 * (already loaded), so a firmware without them just shows no game.
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_GAME_SYS_H
#define HEARBRIDGE_GAME_SYS_H

/* 1 if the lookups exist on this firmware (after the first call). */
int  hb_game_sys_avail(void);
/* Title id of the running game into id (e.g. PPSA01234), "" when none.
 * Returns 1 if a game is running. */
int  hb_game_sys_running(char *id, int max);
/* Title name from /user/appmeta/<id>/param.json, "" if it cannot be read. */
void hb_game_sys_name(const char *id, char *name, int max);

#endif
