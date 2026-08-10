#ifndef TURBO_CHARGE_VALUE_SET_H
#define TURBO_CHARGE_VALUE_SET_H

#include "global.h"

void set_value(const char *file, const char *value);
void set_array_value(char **files, int num, const char *value);
int apply_charge_current(const ChargeCurrentNodes *nodes, const char *value);
int restore_charge_current(const ChargeCurrentNodes *nodes, const char *normal_current);
int write_meizu_wired_level_with_echo(int level, int *found_count, int *success_count);
int restore_meizu_wired_level_permission(void);

#endif
