#ifndef TURBO_CHARGE_PRINTF_WITH_TIME_H
#define TURBO_CHARGE_PRINTF_WITH_TIME_H

void printf_with_time(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
