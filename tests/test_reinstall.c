/* Developed by X-F1REBALL-X. Host tests for re-installing HearBridge:
 * home-screen icon registration on every run (fresh install, already
 * installed, deleted by the user, failures) and the single-instance lock
 * (live, crashed, stale records). */
#include "lock.h"
#include "tile.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

/* ---- fake console installer for tile_register() --------------------- */
static struct {
    int installed;          /* is the title in the console's database */
    int meta;               /* does /user/appmeta/<id> exist */
    int titledir_rc_new, titledir_rc_dup;   /* rc for a new / an installed title */
    int all_rc;
    int exists_rc;
    int n_init, n_term, n_titledir, n_all, n_exists;
} F;

static int f_init(void) { F.n_init++; return 0; }
static int f_init_fail(void) { F.n_init++; return (int)0x80990001u; }
static int f_term(void) { F.n_term++; return 0; }
static int f_titledir(const char *id, const char *dir, void *opt)
{
    (void)opt;
    F.n_titledir++;
    if (strcmp(id, HB_TILE_ID) || strcmp(dir, HB_TILE_ROOT "/")) return -1;
    if (F.installed) return F.titledir_rc_dup;
    if (F.titledir_rc_new) return F.titledir_rc_new;
    F.installed = F.meta = 1;
    return 0;
}
static int f_all(void *opt)
{
    (void)opt;
    F.n_all++;
    if (F.all_rc) return F.all_rc;
    F.installed = F.meta = 1;
    return 0;
}
static int f_exists(const char *id, int *e)
{
    F.n_exists++;
    if (F.exists_rc) return F.exists_rc;
    *e = !strcmp(id, HB_TILE_ID) && F.installed;
    return 0;
}
static int f_meta(const char *id) { return !strcmp(id, HB_TILE_ID) && F.meta; }

static tile_ops fake_ops(void)
{
    tile_ops o;
    memset(&o, 0, sizeof o);
    o.init = f_init; o.term = f_term; o.title_dir = f_titledir;
    o.install_all = f_all; o.app_exists = f_exists; o.meta_exists = f_meta;
    return o;
}

static void test_tile_register(void)
{
    tile_ops o;
    tile_report r;
    char line[600], root[] = "/tmp/hb_tile2_XXXXXX", p[512];
    int i;

    /* Fresh install. */
    memset(&F, 0, sizeof F);
    o = fake_ops();
    memset(&r, 0, sizeof r);
    CHECK(tile_register(&o, &r) == 0 && F.installed && F.n_titledir == 1 && F.n_all == 0 &&
          F.n_init == 1 && F.n_term == 1 && !r.already && r.exists == 1 && !r.meta_before && r.meta_after,
          "fresh install: TitleDir registers, no fallback");
    tile_report_line(&r, line, sizeof line);
    CHECK(strstr(line, "TitleDir found -> 0") && strstr(line, "AppExists yes") && strstr(line, "RESULT ok"),
          "fresh install: report line");

    /* Already installed and not deleted: still registered again on every
     * run; an "already installed" style error is not a failure. */
    for (i = 0; i < 3; i++) {
        memset(&r, 0, sizeof r);
        F.titledir_rc_dup = (int)0x80990015u;    /* made-up "already exists" codes */
        F.all_rc = (int)0x80990016u;
        CHECK(tile_register(&o, &r) == 0 && r.already && r.result == 0 && !r.failed,
              "already installed: re-register on every run, error code treated as success");
    }
    CHECK(F.n_titledir == 4 && F.n_all == 3, "already installed: install called each run (with fallback)");
    tile_report_line(&r, line, sizeof line);
    CHECK(strstr(line, "TitleDir found -> 0x80990015") && strstr(line, "treated as success") &&
          strstr(line, "RESULT ok"), "already installed: logged as treated-as-success");
    memset(&r, 0, sizeof r);
    F.titledir_rc_dup = 0;                      /* firmware that just answers 0 */
    F.all_rc = 0;
    CHECK(tile_register(&o, &r) == 0 && !r.already, "already installed: rc 0 is plain success");

    /* Deleted from the home screen: database entry and appmeta gone, our
     * files (and the old marker) left behind -> registered again. */
    memset(&F, 0, sizeof F);
    memset(&r, 0, sizeof r);
    CHECK(mkdtemp(root) != NULL, "deleted: temp root");
    CHECK(tile_files_put(root, NULL, (const unsigned char *)"PNG", 3, NULL, 0) == 1, "deleted: earlier files");
    snprintf(p, sizeof p, "%s/%s/sce_sys/.hb_registered", root, HB_TILE_ID);
    { FILE *m = fopen(p, "w"); if (m) fclose(m); }
    CHECK(tile_files_put(root, NULL, (const unsigned char *)"PNG", 3, NULL, 0) == 0,
          "deleted: files unchanged (what made 1.0.1 skip)");
    CHECK(tile_files_write(root, NULL, (const unsigned char *)"PNG", 3, NULL, 0) == 0,
          "deleted: files are rewritten anyway");
    CHECK(tile_register(&o, &r) == 0 && F.installed && F.meta && F.n_titledir == 1 && !r.meta_before,
          "deleted: marker ignored, title registered again");
    unlink(p);
    tile_files_drop(root);
    rmdir(root);

    /* Real failure: both calls fail and the console does not have it. */
    memset(&F, 0, sizeof F);
    memset(&r, 0, sizeof r);
    F.titledir_rc_new = (int)0x80990020u; F.all_rc = (int)0x80990021u;
    CHECK(tile_register(&o, &r) == -1 && r.failed && !strcmp(r.failed, "InstallAll") &&
          r.code == (int)0x80990021u && !r.already, "failure: reported with the last code");

    /* TitleDir missing on this firmware -> InstallAll. */
    memset(&F, 0, sizeof F);
    memset(&r, 0, sizeof r);
    o.title_dir = NULL;
    CHECK(tile_register(&o, &r) == 0 && F.n_all == 1 && F.installed && !r.titledir_found,
          "TitleDir missing: InstallAll fallback");
    tile_report_line(&r, line, sizeof line);
    CHECK(strstr(line, "TitleDir NOT FOUND") && strstr(line, "InstallAll -> 0"), "TitleDir missing: report line");
    o = fake_ops();

    /* AppExists unavailable: appmeta decides whether an error is benign. */
    memset(&F, 0, sizeof F);
    memset(&r, 0, sizeof r);
    F.installed = F.meta = 1; F.titledir_rc_dup = 5; F.all_rc = 6;
    o.app_exists = NULL;
    CHECK(tile_register(&o, &r) == 0 && r.already && r.exists == -1, "no AppExists: appmeta present -> ok");
    memset(&r, 0, sizeof r);
    F.meta = 0;
    CHECK(tile_register(&o, &r) == -1, "no AppExists: appmeta missing -> failure");
    o = fake_ops();
    memset(&r, 0, sizeof r);
    F.meta = 1; F.exists_rc = (int)0x80990099u;
    CHECK(tile_register(&o, &r) == 0 && r.exists == -1 && r.already, "AppExists error: falls back to appmeta");

    /* Installer init fails. */
    memset(&F, 0, sizeof F);
    memset(&r, 0, sizeof r);
    o.init = f_init_fail;
    CHECK(tile_register(&o, &r) == -1 && !strcmp(r.failed, "installer init") && F.n_titledir == 0,
          "init failure: nothing else called");
    tile_report_line(&r, line, sizeof line);
    CHECK(strstr(line, "init 0x80990001") && !strstr(line, "TitleDir"), "init failure: report line");
    {
        char tiny[16];
        CHECK(tile_report_line(&r, tiny, sizeof tiny) == 15 && tiny[15] == 0, "report truncates safely");
    }
    memset(&r, 0, sizeof r);
    r.files_failed = 1; r.files_errno = EACCES; r.result = -1; r.failed = "writing"; r.code = EACCES;
    tile_report_line(&r, line, sizeof line);
    CHECK(strstr(line, "files ERROR (errno 13)") && !strstr(line, "init"), "write error: report line");
}

static void test_lock(void)
{
    char dir[] = "/tmp/hb_lock_XXXXXX", path[300];
    FILE *f;
    int err = -1, st;
    pid_t child = -1;
    int pfd[2];
    char c;

    CHECK(mkdtemp(dir) != NULL, "lock temp dir");
    snprintf(path, sizeof path, "%s/hearbridge.lock", dir);
    CHECK(lock_take_ex(path, &err) == LOCK_OK && err == 0, "lock taken");
    f = fopen(path, "r");
    {
        long pid = 0, boot = -1; char m = 0;
        CHECK(f && fscanf(f, "%ld %ld %c", &pid, &boot, &m) == 3 && pid == (long)getpid() && m == 'F',
              "record: pid + flock mark");
        if (f) fclose(f);
    }
    lock_release();

    /* A live instance (child holding the flock) blocks; once it dies (crash)
     * the lock is free again with no clean-up. */
    if (pipe(pfd) == 0 && (child = fork()) == 0) {
        close(pfd[0]);
        if (lock_take_ex(path, NULL) == LOCK_OK) { c = 'y'; } else { c = 'n'; }
        if (write(pfd[1], &c, 1) != 1) _exit(2);
        pause();
        _exit(0);
    }
    close(pfd[1]);
    c = 0;
    CHECK(read(pfd[0], &c, 1) == 1 && c == 'y', "child took the lock");
    close(pfd[0]);
    CHECK(lock_take_ex(path, &err) == LOCK_BUSY, "live instance (flock held) -> busy");
    if (child > 0) {
        kill(child, SIGKILL);
        waitpid(child, &st, 0);
    }
    CHECK(lock_take_ex(path, &err) == LOCK_OK, "crashed instance: lock free, no stale block");
    lock_release();

    /* Stale records left behind in the file. */
    f = fopen(path, "w");
    if (f) { fprintf(f, "999999 0 F\n"); fclose(f); }
    CHECK(lock_take_ex(path, &err) == LOCK_OK, "stale lock: dead pid -> taken over");
    lock_release();
    f = fopen(path, "w");
    if (f) { fprintf(f, "%ld 0 F\n", (long)getppid()); fclose(f); }
    CHECK(lock_take_ex(path, &err) == LOCK_OK, "stale lock: pid reused by another process -> taken over");
    lock_release();
    f = fopen(path, "w");
    if (f) { fprintf(f, "%ld 0\n", (long)getppid()); fclose(f); }
    CHECK(lock_take_ex(path, &err) == LOCK_BUSY, "1.0.1-style record with a live pid -> busy (old instance)");
    f = fopen(path, "w");
    if (f) { fprintf(f, "999999 0\n"); fclose(f); }
    CHECK(lock_take_ex(path, &err) == LOCK_OK, "1.0.1-style record with a dead pid -> taken over");
    lock_release();
    f = fopen(path, "w");
    if (f) { fprintf(f, "garbage\n"); fclose(f); }
    CHECK(lock_take_ex(path, &err) == LOCK_OK, "unreadable record -> taken over");
    lock_release();

    /* Pure decision table. */
    CHECK(lock_decide(-1, EWOULDBLOCK, 3, 5, 1, 1, 1, 9) == LOCK_BUSY, "decide: flock held -> busy");
    CHECK(lock_decide(-1, EOPNOTSUPP, 2, 9999999, 1, 0, 1, 9) == LOCK_OK, "decide: no flock, dead pid -> ok");
    CHECK(lock_decide(-1, EOPNOTSUPP, 2, (long)getppid(), 0, 0, 0, 9) == LOCK_BUSY, "decide: no flock, live pid -> busy");
    CHECK(lock_decide(0, 0, 2, (long)getppid(), 100, 0, 200, 9) == LOCK_OK, "decide: record from an earlier boot -> ok");
    CHECK(lock_decide(0, 0, 3, 9, 0, 1, 0, 9) == LOCK_OK, "decide: our own pid -> ok");

    CHECK(lock_take_ex("/nonexistent-dir/hearbridge.lock", &err) == LOCK_NO_WRITE && err == ENOENT,
          "unwritable folder -> no-write with errno (not 'already running')");
    CHECK(lock_take("/nonexistent-dir/hearbridge.lock") == 0, "old API still reports failure");
    unlink(path);
    rmdir(dir);
}

int main(void)
{
    test_tile_register();
    test_lock();
    if (fails) printf("%d test(s) FAILED\n", fails);
    else printf("ALL OK (0 failures)\n");
    return fails != 0;
}
