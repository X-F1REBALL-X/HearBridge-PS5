/* Developed by X-F1REBALL-X. Host test for the home-screen tile files. */
#define _DEFAULT_SOURCE
#include "tile.h"
#include "icon_png.h"
#include "start_html.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

static long fsize(const char *p) { struct stat st; return stat(p, &st) ? -1 : (long)st.st_size; }

int main(void)
{
    char root[] = "/tmp/hb_tile_XXXXXX", p[512], js[2048], small[16];
    int n;

    CHECK(sizeof hb_icon_png > 8 && !memcmp(hb_icon_png, "\x89PNG\r\n\x1a\n", 8), "embedded icon is a PNG");
    CHECK(hb_icon_png[16] == 0 && hb_icon_png[18] == 2 && hb_icon_png[19] == 0 &&
          hb_icon_png[20] == 0 && hb_icon_png[22] == 2 && hb_icon_png[23] == 0, "icon is 512x512");
    n = tile_manifest(js, sizeof js, NULL);
    CHECK(n > 0 && strstr(js, "\"titleId\": \"" HB_TILE_ID "\"") &&
          strstr(js, "\"deeplinkUri\": \"" HB_TILE_URL "\"") &&
          strstr(js, "\"applicationCategoryType\": 0,") && strstr(js, "\"contentBadgeType\": 1,"),
          "manifest fields (tile under Games)");
    CHECK(!strstr(js, "65536"), "manifest: no longer a Media app");
    CHECK(!tile_needs_reinstall(NULL, 0) && !tile_needs_reinstall("1", 0), "tile version: nothing installed, nothing to redo");
    CHECK(tile_needs_reinstall(NULL, 1), "tile version: installed by 1.1.0 (no version file) -> reinstall once");
    CHECK(tile_needs_reinstall("1\n", 1), "tile version: older version -> reinstall");
    CHECK(!tile_needs_reinstall(HB_TILE_VERSION "\n", 1) && !tile_needs_reinstall(HB_TILE_VERSION "\r\n", 1),
          "tile version: current version -> left alone");
    CHECK(strstr(js, "\"defaultLanguage\": \"en-US\"") && strstr(js, "\"he-IL\""), "manifest languages");
    CHECK(tile_manifest(small, sizeof small, NULL) == -1, "manifest refuses a short buffer");
    {
        char j2[2048];
        CHECK(tile_manifest(j2, sizeof j2, HB_TILE_START_URL) > 0 &&
              strstr(j2, "\"deeplinkUri\": \"file:///user/app/" HB_TILE_ID "/start.html\""),
              "manifest takes the fallback-page link");
    }
    CHECK(strstr((const char *)hb_start_html, "127.0.0.1:8090") &&
          strstr((const char *)hb_start_html, "HearBridge is not running") &&
          strstr((const char *)hb_start_html, "ar:[") && !strstr((const char *)hb_start_html, "he:["),
          "fallback page probes the server, has languages, no Hebrew");

    if (!mkdtemp(root)) return 1;
    CHECK(tile_files_put(root, NULL, hb_icon_png, sizeof hb_icon_png, hb_start_html, HB_START_HTML_LEN) == 1, "first put writes");
    snprintf(p, sizeof p, "%s/%s/sce_sys/icon0.png", root, HB_TILE_ID);
    CHECK(fsize(p) == (long)sizeof hb_icon_png, "icon0.png written");
    snprintf(p, sizeof p, "%s/%s/sce_sys/param.json", root, HB_TILE_ID);
    CHECK(fsize(p) == n, "param.json written");
    snprintf(p, sizeof p, "%s/%s/start.html", root, HB_TILE_ID);
    CHECK(fsize(p) == (long)HB_START_HTML_LEN, "start.html written");
    CHECK(tile_files_put(root, NULL, hb_icon_png, sizeof hb_icon_png, hb_start_html, HB_START_HTML_LEN) == 0, "second put is a no-op");
    CHECK(tile_files_put(root, HB_TILE_START_URL, hb_icon_png, sizeof hb_icon_png, NULL, 0) == 1, "changed link is rewritten");
    CHECK(tile_files_put(root, NULL, (const unsigned char *)"x", 1, NULL, 0) == 1, "changed icon is rewritten");
    tile_files_drop(root);
    snprintf(p, sizeof p, "%s/%s", root, HB_TILE_ID);
    CHECK(fsize(p) == -1, "drop removes the folder");
    CHECK(tile_files_put("/nonexistent/dir", NULL, hb_icon_png, sizeof hb_icon_png, NULL, 0) == -1, "bad root fails");
    rmdir(root);
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
