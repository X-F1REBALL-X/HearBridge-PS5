/* Settings backup / restore: one JSON file with the saved headsets
 * (paired.ini + headset.ini, link keys included so they reconnect without
 * pairing again), their settings (prefs/), the gain and the per-game
 * profiles. Written to a USB drive (/mnt/usbN/hearbridge-backup) or the
 * console (/data/hearbridge-backup) as hearbridge-YYYYMMDD-HHMMSS.json plus
 * hearbridge-latest.json; index.txt lists them, newest first (the payload
 * has no directory listing). Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_BACKUP_H
#define HEARBRIDGE_BACKUP_H

#define HB_BACKUP_INTERNAL "/data/hearbridge-backup"
#define HB_BACKUP_SUB      "hearbridge-backup"
#define HB_BACKUP_LATEST   "hearbridge-latest.json"
#define HB_BACKUP_KEEP     20
#define HB_BACKUP_MAX      65536   /* whole JSON */
#define HB_BACKUP_FILE_MAX 8192    /* one settings file */
#define HB_BACKUP_FILES    16
#define HB_BACKUP_NAME     40

typedef struct {
    char name[32];           /* relative to the state dir, e.g. prefs/AABBCCDDEEFF.txt */
    const char *text;
    int len;
} hb_bfile;

/* hearbridge-YYYYMMDD-HHMMSS.json (UTC) for epoch seconds. */
void hb_backup_stamp(long epoch_s, char *out, int max);
/* "2026-10-10 02:13" from such a name ("" for latest/unknown). */
void hb_backup_when(const char *name, char *out, int max);
/* A file name Restore may open: a stamp name or the latest one. */
int  hb_backup_name_ok(const char *name);
/* A settings file a backup may carry / restore may write. */
int  hb_backup_file_ok(const char *rel);
/* prefs/<ADDR>.txt for every addr= in a paired.ini text. Returns count. */
int  hb_backup_prefs_names(const char *paired_ini, char (*names)[32], int max);

int  hb_backup_build(const hb_bfile *f, int n, const char *version, long epoch_s,
                     char *out, int max);
/* Files from a backup JSON; their text is copied into buf (NUL-terminated
 * each). Returns the count, -1 when it is not a HearBridge backup. Names
 * that fail hb_backup_file_ok are skipped. */
int  hb_backup_parse(const char *json, hb_bfile *f, int max, char *buf, int bufmax);

/* index.txt: put name first (no duplicates, at most HB_BACKUP_KEEP). */
int  hb_backup_index_add(const char *old_text, const char *name, char *out, int max);
/* Valid names from index.txt, newest first. */
int  hb_backup_index_list(const char *text, char (*names)[HB_BACKUP_NAME], int max);

/* ---- file side (plain stdio, host testable with temp dirs) ---- */
/* First /mnt/usb0..7 (under mnt_root) that is a mounted drive (a different
 * device than mnt_root itself). Fills dir with <usb>/hearbridge-backup. */
int  hb_backup_find_usb(const char *mnt_root, char *dir, int max);
/* Backup of state_dir into dest_dir (created). name_out gets the file name.
 * 1 ok, 0 failed. */
int  hb_backup_save(const char *state_dir, const char *dest_dir, const char *version,
                    long epoch_s, char *name_out, int name_max);
/* Backups listed in dest_dir/index.txt that still exist, newest first, then
 * hearbridge-latest.json if present and nothing else is. */
int  hb_backup_list(const char *dest_dir, char (*names)[HB_BACKUP_NAME], int max);
/* Writes the files of dest_dir/name into state_dir (each atomically).
 * Returns files written, -1 not readable / not a backup. */
int  hb_backup_restore(const char *state_dir, const char *dest_dir, const char *name);
/* Same from a backup JSON already in memory (uploaded from the page). */
int  hb_backup_restore_text(const char *state_dir, const char *json);
/* The backup JSON of state_dir into out (for a browser download). Length or -1. */
int  hb_backup_make(const char *state_dir, const char *version, long epoch_s, char *out, int max);

#endif
