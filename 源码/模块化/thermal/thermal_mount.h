#ifndef TURBO_CHARGE_THERMAL_MOUNT_H
#define TURBO_CHARGE_THERMAL_MOUNT_H

#include "global.h"

const char *meizu_thermal_scheme_name(int scheme);
void mount_thermal_files(void);
void umount_thermal_files(void);
void sync_thermal_mount_mode(int is_charging, MountModeState *state);

#endif
