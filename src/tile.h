/* Developed by X-F1REBALL-X.
 * Home-screen tile: a tiny title folder <root>/<id>/sce_sys/{param.json,icon0.png}
 * whose param.json carries a deep link to the control page. The file side lives
 * here (host-testable); tile_sys.c asks the console to register / drop it. */
#ifndef HB_TILE_H
#define HB_TILE_H

#include <stddef.h>

#define HB_TILE_ID    "HRBG00001"
#define HB_TILE_NAME  "HearBridge"
#define HB_TILE_URL   "http://127.0.0.1:8090/"
#define HB_TILE_ROOT  "/user/app"

/* Fallback page written next to the tile (start.html): checks whether
 * HearBridge answers and either opens the control page or explains, in the
 * user's language, that the ELF must be loaded first. */
#define HB_TILE_START_URL "file://" HB_TILE_ROOT "/" HB_TILE_ID "/start.html"

/* param.json text for the tile with this deep link; length, or -1. */
int tile_manifest(char *out, size_t cap, const char *url);

/* Create/refresh the folder under root (param.json, icon0.png and, when
 * html is given, start.html). 1 = something written, 0 = already identical,
 * -1 = error. */
int tile_files_put(const char *root, const char *url,
                   const unsigned char *png, size_t png_len,
                   const unsigned char *html, size_t html_len);

/* Delete exactly what tile_files_put created. */
void tile_files_drop(const char *root);

/* Console side (tile_sys.c). url NULL = HB_TILE_URL. 0 on success. */
int tile_install(const char *url);
int tile_uninstall(void);

#endif
