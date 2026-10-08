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

/* Where the console keeps the metadata of registered titles. A folder for
 * our id there means the registration really happened. */
#define HB_TILE_APPMETA "/user/appmeta/" HB_TILE_ID

/* Should the folder be (re)registered? Only skip when nothing changed, our
 * marker says an earlier registration worked AND the console's own
 * metadata folder for the id exists (a marker alone can be stale). */
int tile_need_register(int files_changed, int marker_exists, int appmeta_exists);

/* What tile_install() did, step by step (for the diagnostics report). */
typedef struct {
    int files;              /* tile_files_put: 1 written, 0 same, -1 error */
    int files_errno;
    int marker, appmeta_before, appmeta_after;
    int skipped;            /* trusted the earlier registration */
    unsigned long long authid_before, authid_used;
    int init_rc, init_errno;
    int titledir_found, titledir_called, titledir_rc, titledir_errno;
    int all_found, all_called, all_rc, all_errno;
    int result;             /* 0 = registered (or already registered) */
    int code;               /* failing return code for the toast */
    const char *failed;     /* failing step for the toast, or NULL */
} tile_report;

/* One-line summary of the steps ("files written; init 0; TitleDir 0 ..."). */
int tile_report_line(const tile_report *r, char *out, size_t cap);

/* Console side (tile_sys.c). url NULL = HB_TILE_URL. 0 on success.
 * rep (may be NULL) receives the step-by-step results. */
int tile_install(const char *url, tile_report *rep);
int tile_uninstall(void);

#endif
