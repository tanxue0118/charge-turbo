#ifndef TURBO_CHARGE_PRINTF_WITH_TIME_H
#define TURBO_CHARGE_PRINTF_WITH_TIME_H

#include <stdarg.h>

void printf_with_time(const char *format, ...) __attribute__((format(printf, 1, 2)));
void vprintf_with_time(const char *format, va_list ap);

#endif
