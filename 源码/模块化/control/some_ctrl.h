#ifndef TURBO_CHARGE_SOME_CTRL_H
#define TURBO_CHARGE_SOME_CTRL_H

#include "global.h"

void handle_meizu_generation_change(int *last_meizu_thermal_key,
                                    MountModeState *thermal_mount_state,
                                    int is_charging);
void step_charge_ctl(const char *value);
void charge_ctl(const char *value);
void restore_meizu_wired_level(MeizuWiredLevelState *state);
void sync_meizu_wired_level(int is_charging, MeizuWiredLevelState *state);
int read_external_power_state(void);
void power_ctl(PowerControlState *state,
               int capacity_available,
               int stop_supported,
               int *stop_requested,
               int *bypass_requested);
void bypass_charge_ctl(int android_version,
                       char *last_appname,
                       int *app_bypass_requested,
                       int *screen_is_off);
void sync_bypass_control(BypassState *bypass_state,
                         PowerControlState *power_state,
                         int stop_requested,
                         int bypass_requested,
                         const ChargeCurrentNodes *nodes,
                         const char *normal_current);
void restore_charge_control(BypassState *bypass_state,
                            PowerControlState *power_state,
                            const ChargeCurrentNodes *nodes,
                            const char *normal_current);
int is_bypass_active(const BypassState *state);
void apply_step_charge_policy(uchar step_charge, const char *power);

#endif
