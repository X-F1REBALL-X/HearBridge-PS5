/* Built-in web control page + JSON API. Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_HTTP_H
#define HEARBRIDGE_HTTP_H

#include "ctl.h"

#define HB_HTTP_PORT 8090

/* Pure request handler (host testable): req = raw HTTP request bytes.
 * Writes a complete HTTP response into out; returns its length. */
int http_handle(hb_ctl *c, const char *req, int reqlen, char *out, int max);

/* Start the server thread on HB_HTTP_PORT (tries the next few ports if
 * taken). Fills url ("http://ip:port") on success; returns port or 0. */
int  http_start(hb_ctl *c, char *url, int url_max);
void http_stop(void);

#endif
