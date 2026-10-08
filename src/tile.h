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

/* Write param.json, icon0.png and (when html is given) start.html every
 * time, even when they are already identical. 0 on success, -1 on error. */
int tile_files_write(const char *root, const char *url,
                     const unsigned char *png, size_t png_len,
                     const unsigned char *html, size_t html_len);

/* Where the console keeps the metadata of registered titles. */
#define HB_TILE_APPMETA "/user/appmeta/" HB_TILE_ID

/* Legacy marker written by 1.0.1 and the first fw13.60 build. It is no
 * longer consulted (it survives a delete from the home screen) and is
 * removed on start. */
#define HB_TILE_LEGACY_MARK HB_TILE_ROOT "/" HB_TILE_ID "/sce_sys/.hb_registered"

/* The installer calls tile_register() needs. Any pointer may be NULL
 * (= not available on this firmware). */
typedef struct {
    int (*init)(void);
    int (*term)(void);
    int (*title_dir)(const char *title_id, const char *dir, void *opt);
    int (*install_all)(void *opt);
    int (*app_exists)(const char *title_id, int *exists);
    int (*meta_exists)(const char *title_id);   /* 1 if /user/appmeta/<id> exists */
} tile_ops;

/* What tile_install() did, step by step (log line / diagnostics). */
typedef struct {
    int files_errno;        /* tile_files_write failed with this errno */
    int files_failed;
    int legacy_marker;      /* the old marker was present (and removed) */
    unsigned long long authid_before, authid_used;
    int meta_before, meta_after;     /* /user/appmeta/<id> before / after */
    int init_called, init_rc, init_errno;
    int titledir_found, titledir_called, titledir_rc, titledir_errno;
    int all_found, all_called, all_rc, all_errno;
    int exists_found, exists_rc, exists;   /* exists: 1 yes, 0 no, -1 unknown */
    int already;            /* install returned an error but the title is installed */
    int plain_failed;       /* the attempt with the payload's own rights failed ... */
    int plain_code;         /* ... with this code ... */
    const char *plain_at;   /* ... at this step; then retried with raised rights */
    int result;             /* 0 = icon registered */
    int code;               /* failing return code for the toast */
    const char *failed;     /* failing step for the toast, or NULL */
} tile_report;

/* Register HB_TILE_ID from HB_TILE_ROOT on every call (no "already
 * installed" shortcut): per-title install first, InstallAll(0) if that is
 * missing or fails. A non-zero code counts as success when the console
 * says the title is installed afterwards (AppExists = 1; if AppExists is
 * unavailable, when /user/appmeta/<id> exists), so re-registering an
 * installed title never reports a failure. Fills *r; returns r->result. */
int tile_register(const tile_ops *ops, tile_report *r);

/* Runtime fallback instead of a per-firmware build: tile_register() with
 * the payload's own rights first (the path tested on fw 10.20); only when
 * that fails, raise() (e.g. switch to ShellCore, needed on newer firmware
 * such as 13.60), register once more and lower() again. raise returns the
 * authid now in effect (stored in r->authid_used). raise/lower may be NULL
 * (no retry). Returns r->result. */
int tile_register_fallback(const tile_ops *ops, tile_report *r,
                           unsigned long long (*raise)(void *ctx),
                           void (*lower)(void *ctx), void *ctx);

/* One-line summary of the steps ("init 0; TitleDir 0 ..."). */
int tile_report_line(const tile_report *r, char *out, size_t cap);

/* Console side (tile_sys.c). url NULL = HB_TILE_URL. 0 on success.
 * rep (may be NULL) receives the step-by-step results. */
int tile_install(const char *url, tile_report *rep);
int tile_uninstall(void);

#endif
