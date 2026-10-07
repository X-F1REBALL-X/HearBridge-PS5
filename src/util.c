/* util.c - clock source. */
#include "util.h"

#include <time.h>

long (*ph_clock_hook)(void) = 0;

long now_ms(void)
{
    struct timespec t;

    if (ph_clock_hook)
        return ph_clock_hook();
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000L + (long)(t.tv_nsec / 1000000L);
}
