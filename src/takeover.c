/* takeover.c - see takeover.h. Developed by X-F1REBALL-X. */
#include "takeover.h"
#include "log.h"
#include "util.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void hb_set_proc_name(const char *name)
{
    /* The PS5 kernel keeps the main thread's name in p_comm (klogsrv,
     * ftpsrv and the other payloads name themselves the same way). */
    if (syscall(SYS_thr_set_name, -1, name) != 0)
        log_line("proc: could not set the process name (errno %d)", errno);
}

long hb_find_pid(const char *name)
{
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
    size_t sz = 0;
    unsigned char *buf, *p;
    long found = -1, self = (long)getpid();

    if (sysctl(mib, 4, NULL, &sz, NULL, 0) != 0 || !sz) return -1;
    sz += sz / 4;                       /* processes may start meanwhile */
    if (!(buf = malloc(sz))) return -1;
    if (sysctl(mib, 4, buf, &sz, NULL, 0) != 0) {
        free(buf);
        return -1;
    }
    /* kinfo_proc as the PS5 kernel lays it out (same offsets klogsrv
     * uses): ki_structsize at 0, ki_pid at 72, the thread name at 447. */
    for (p = buf; p + 447 + 20 <= buf + sz; ) {
        int len;
        long pid;
        memcpy(&len, p, sizeof len);
        if (len < 467 || p + len > buf + sz) break;
        pid = (long)*(pid_t *)(void *)(p + 72);
        if (pid != self && !strncmp((const char *)p + 447, name, 19)) found = pid;
        p += len;
    }
    free(buf);
    return found;
}

int hb_wait_pid_gone(long pid, int ms)
{
    int t;
    if (pid <= 1) return 1;
    for (t = 0; t <= ms; t += 100) {
        if (kill((pid_t)pid, 0) != 0 && errno == ESRCH) return 1;
        usleep(100 * 1000);
    }
    return 0;
}

static void touch(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    if (fd >= 0) close(fd);
}

long hb_stop_named(const char *name, const char *stop_path, int graceful_ms)
{
    long pid = hb_find_pid(name);
    if (pid <= 1) return 0;
    log_line("takeover: another %s is running (pid %ld) - asking it to stop", name, pid);
    touch(stop_path);
    kill((pid_t)pid, SIGTERM);
    if (!hb_wait_pid_gone(pid, graceful_ms)) {
        log_line("takeover: pid %ld still running after %d ms - SIGKILL", pid, graceful_ms);
        kill((pid_t)pid, SIGKILL);
        (void)hb_wait_pid_gone(pid, 3000);
    }
    unlink(stop_path);
    return pid;
}

/* 1 = something listens on the port. */
static int port_busy(int port)
{
    struct sockaddr_in a;
    int one = 1, busy = 0;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 0;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0) busy = errno == EADDRINUSE;
    close(s);
    return busy;
}

int hb_port_takeover(int port, const char *stop_path, int ms)
{
    int t;
    if (!port_busy(port)) return 0;
    log_line("takeover: port %d is still in use - asking a running copy to stop", port);
    touch(stop_path);
    for (t = 0; t < ms; t += 250) {
        usleep(250 * 1000);
        if (!port_busy(port)) {
            unlink(stop_path);
            log_line("takeover: port %d free after %d ms", port, t + 250);
            usleep(500 * 1000);      /* its Bluetooth teardown runs before the page closes */
            return 1;
        }
    }
    unlink(stop_path);
    log_line("takeover: port %d still in use after %d ms (not a HearBridge copy?)", port, ms);
    return -1;
}
