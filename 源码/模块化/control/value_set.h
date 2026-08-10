#ifndef TURBO_CHARGE_VALUE_SET_H
#define TURBO_CHARGE_VALUE_SET_H

#define SET_VALUE_FAILED (-1)
#define SET_VALUE_SKIPPED 0
#define SET_VALUE_OK 1

int set_value(const char *file, const char *value);
int set_array_value(char **files, int num, const char *value);
int write_meizu_wired_level_with_echo(int level, int *found_count, int *success_count);
int restore_meizu_wired_level_permission(void);

#endif
