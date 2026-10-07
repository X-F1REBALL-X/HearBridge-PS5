/* log.c - timestamped log lines, flushed immediately so nothing is lost if
 * the payload is killed. */
#include "log.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>

static FILE *log_fp;

int log_open(const char *path)
{
    FILE *f;

    if (!path)
        return 0;
    f = fopen(path, "a");
    if (!f)
        return 0;
    log_close();
    log_fp = f;
    return 1;
}

void log_close(void)
{
    if (log_fp)
        fclose(log_fp);
    log_fp = NULL;
}

void log_line(const char *fmt, ...)
{
    FILE *dst = log_fp ? log_fp : stderr;
    long t = now_ms();
    va_list args;

    fprintf(dst, "[%ld.%03ld] ", t / 1000, t % 1000);
    va_start(args, fmt);
    vfprintf(dst, fmt, args);
    va_end(args);
    fputc('\n', dst);
    fflush(dst);
}
