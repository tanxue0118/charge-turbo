#define _GNU_SOURCE

#include "global.h"
#include "printf_with_time.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>
static void get_utc8_time(struct tm *ptm)
{
    time_t cur_time = time(NULL);
    cur_time += 8 * 3600;
    gmtime_r(&cur_time, ptm);

    ptm->tm_year += 1900;
    ptm->tm_mon += 1;
}

void printf_with_time(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vprintf_with_time(format, ap);
    va_end(ap);
}

void vprintf_with_time(const char *format, va_list ap)
{
    char buffer[LOG_BUF_SIZE] = {0};
    struct tm time_now;

    vsnprintf(buffer, sizeof(buffer), format, ap);

    get_utc8_time(&time_now);

    printf("[ %04d.%02d.%02dT%02d:%02d:%02d UTC+8 ] %s\n",
           time_now.tm_year,
           time_now.tm_mon,
           time_now.tm_mday,
           time_now.tm_hour,
           time_now.tm_min,
           time_now.tm_sec,
           buffer);

    fflush(stdout);
}
