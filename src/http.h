/* Built-in web control page + JSON API. Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_HTTP_H
#define HEARBRIDGE_HTTP_H

#include "ctl.h"

#define HB_HTTP_PORT 8090

/* Placeholder in the page (index.html) replaced by hb_ctl.token when the
 * page is served; same length as the token. */
#define HB_TOKEN_SLOT "HBTOKENxxxxxxxxxxxxxxxxxxxxxxxxx"

/* Reads (GET): /, /api/status, /api/devices, /api/saved, /api/diag.
 * Everything that changes state must be a POST carrying the page's token
 * in an X-HB-Token header (405 for GET, 403 for a missing/wrong token), so
 * other web pages or LAN devices cannot drive HearBridge blindly. */
/* Pure request handler (host testable): req = raw HTTP request bytes.
 * Writes a complete HTTP response into out; returns its length. */
/* 1 when req is "GET /api/gameicon?id=<title id>" (id copied out). */
int http_gameicon_id(const char *req, int reqlen, char *id, int idmax);

int http_handle(hb_ctl *c, const char *req, int reqlen, char *out, int max);

/* Start the server thread on HB_HTTP_PORT (tries the next few ports if
 * taken). Fills url ("http://ip:port") on success; returns port or 0. */
int  http_start(hb_ctl *c, char *url, int url_max);
void http_stop(void);

#endif
