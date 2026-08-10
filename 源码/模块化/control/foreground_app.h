#ifndef TURBO_CHARGE_FOREGROUND_APP_H
#define TURBO_CHARGE_FOREGROUND_APP_H

#include <stddef.h>
#include <time.h>

int check_android_version(void);
void get_foreground_app(char *out, size_t out_size);
void start_foreground_thread_if_needed(int android_version);
void stop_foreground_thread(void);
int load_bypass_app_list(char ***apps, int *app_num, time_t *last_mtime);

#endif
