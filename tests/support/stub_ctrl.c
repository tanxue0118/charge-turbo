#include "stub_ctrl.h"

#include "global.h"
#include "some_ctrl.h"
#include "temp_simulation.h"

#include <stdio.h>
#include <string.h>

static int external_power_state = -1;
static int external_power_calls = 0;

static char last_charge_value[32];
static int charge_ctl_calls = 0;
static int step_charge_ctl_calls = 0;

static int power_ctl_stop = 0;
static int power_ctl_bypass = 0;
static int power_ctl_calls = 0;
static int power_ctl_capacity_available = -1;
static int power_ctl_stop_supported = -1;

static int bypass_active = 0;
static int sync_bypass_calls = 0;
static int sync_bypass_stop_requested = -1;
static int sync_bypass_requested = -1;
static char sync_bypass_current[32];
static int restore_charge_calls = 0;

static int bypass_charge_ctl_calls = 0;
static int bypass_charge_ctl_app = 0;
static int bypass_charge_ctl_screen_off = 0;

static int sync_meizu_calls = 0;
static int sync_meizu_charging = -1;
static int restore_meizu_calls = 0;
static int meizu_generation_calls = 0;

static int apply_step_charge_calls = 0;
static char apply_step_charge_power[32];

static int temp_mc_value = 0;
static int temp_mc_result = 0;
static int read_temp_mc_calls = 0;
static int simulated_temp_mc = -1;
static int apply_temp_simulation_calls = 0;
static int cleanup_temp_simulation_calls = 0;
static int option_generation_change_calls = 0;

void stub_ctrl_reset(void)
{
    external_power_state = -1;
    external_power_calls = 0;

    last_charge_value[0] = '\0';
    charge_ctl_calls = 0;
    step_charge_ctl_calls = 0;

    power_ctl_stop = 0;
    power_ctl_bypass = 0;
    power_ctl_calls = 0;
    power_ctl_capacity_available = -1;
    power_ctl_stop_supported = -1;

    bypass_active = 0;
    sync_bypass_calls = 0;
    sync_bypass_stop_requested = -1;
    sync_bypass_requested = -1;
    sync_bypass_current[0] = '\0';
    restore_charge_calls = 0;

    bypass_charge_ctl_calls = 0;
    bypass_charge_ctl_app = 0;
    bypass_charge_ctl_screen_off = 0;

    sync_meizu_calls = 0;
    sync_meizu_charging = -1;
    restore_meizu_calls = 0;
    meizu_generation_calls = 0;

    apply_step_charge_calls = 0;
    apply_step_charge_power[0] = '\0';

    temp_mc_value = 0;
    temp_mc_result = 0;
    read_temp_mc_calls = 0;
    simulated_temp_mc = -1;
    apply_temp_simulation_calls = 0;
    cleanup_temp_simulation_calls = 0;
    option_generation_change_calls = 0;
}

void stub_set_external_power_state(int state)
{
    external_power_state = state;
}

int stub_read_external_power_calls(void)
{
    return external_power_calls;
}

const char *stub_last_charge_ctl_value(void)
{
    return last_charge_value;
}

int stub_charge_ctl_calls(void)
{
    return charge_ctl_calls;
}

int stub_step_charge_ctl_calls(void)
{
    return step_charge_ctl_calls;
}

void stub_set_power_ctl_result(int stop_requested, int bypass_requested)
{
    power_ctl_stop = stop_requested;
    power_ctl_bypass = bypass_requested;
}

int stub_power_ctl_calls(void)
{
    return power_ctl_calls;
}

int stub_last_power_ctl_capacity_available(void)
{
    return power_ctl_capacity_available;
}

int stub_last_power_ctl_stop_supported(void)
{
    return power_ctl_stop_supported;
}

void stub_set_bypass_active(int active)
{
    bypass_active = active;
}

int stub_sync_bypass_calls(void)
{
    return sync_bypass_calls;
}

int stub_last_sync_bypass_stop_requested(void)
{
    return sync_bypass_stop_requested;
}

int stub_last_sync_bypass_requested(void)
{
    return sync_bypass_requested;
}

const char *stub_last_sync_bypass_current(void)
{
    return sync_bypass_current;
}

int stub_restore_charge_calls(void)
{
    return restore_charge_calls;
}

int stub_bypass_charge_ctl_calls(void)
{
    return bypass_charge_ctl_calls;
}

void stub_set_bypass_charge_ctl_result(int app_bypass_requested, int screen_is_off)
{
    bypass_charge_ctl_app = app_bypass_requested;
    bypass_charge_ctl_screen_off = screen_is_off;
}

int stub_sync_meizu_wired_calls(void)
{
    return sync_meizu_calls;
}

int stub_last_sync_meizu_charging(void)
{
    return sync_meizu_charging;
}

int stub_restore_meizu_wired_calls(void)
{
    return restore_meizu_calls;
}

int stub_meizu_generation_calls(void)
{
    return meizu_generation_calls;
}

int stub_apply_step_charge_calls(void)
{
    return apply_step_charge_calls;
}

const char *stub_last_apply_step_charge_power(void)
{
    return apply_step_charge_power;
}

void stub_set_temp_mc(int temp_mc, int result)
{
    temp_mc_value = temp_mc;
    temp_mc_result = result;
}

int stub_read_temp_mc_calls(void)
{
    return read_temp_mc_calls;
}

void stub_set_simulated_temp_mc(int temp_mc)
{
    simulated_temp_mc = temp_mc;
}

int stub_apply_temp_simulation_calls(void)
{
    return apply_temp_simulation_calls;
}

int stub_cleanup_temp_simulation_calls(void)
{
    return cleanup_temp_simulation_calls;
}

int stub_option_generation_change_calls(void)
{
    return option_generation_change_calls;
}

/* 以下为 some_ctrl.h 的替身实现 */

int read_external_power_state(void)
{
    external_power_calls++;

    return external_power_state;
}

void charge_ctl(const char *value)
{
    charge_ctl_calls++;
    snprintf(last_charge_value, sizeof(last_charge_value), "%s", value ? value : "");
}

void step_charge_ctl(const char *value)
{
    step_charge_ctl_calls++;
}

void apply_step_charge_policy(uchar step_charge, const char *power)
{
    apply_step_charge_calls++;
    snprintf(apply_step_charge_power, sizeof(apply_step_charge_power), "%s", power ? power : "");
}

void power_ctl(PowerControlState *state,
               int capacity_available,
               int stop_supported,
               int *stop_requested,
               int *bypass_requested)
{
    power_ctl_calls++;
    power_ctl_capacity_available = capacity_available;
    power_ctl_stop_supported = stop_supported;

    if (stop_requested) *stop_requested = power_ctl_stop;
    if (bypass_requested) *bypass_requested = power_ctl_bypass;
}

void bypass_charge_ctl(int android_version,
                       char *last_appname,
                       int *app_bypass_requested,
                       int *screen_is_off)
{
    bypass_charge_ctl_calls++;

    if (app_bypass_requested) *app_bypass_requested = bypass_charge_ctl_app;
    if (screen_is_off) *screen_is_off = bypass_charge_ctl_screen_off;
}

int is_bypass_active(const BypassState *state)
{
    return bypass_active;
}

void sync_bypass_control(BypassState *bypass_state,
                         PowerControlState *power_state,
                         int stop_requested,
                         int bypass_requested,
                         char **current_max_file,
                         int current_max_file_num,
                         char **current_limit_file,
                         int current_limit_file_num,
                         const char *normal_current)
{
    sync_bypass_calls++;
    sync_bypass_stop_requested = stop_requested;
    sync_bypass_requested = bypass_requested;
    snprintf(sync_bypass_current, sizeof(sync_bypass_current), "%s",
             normal_current ? normal_current : "");
}

void restore_charge_control(BypassState *bypass_state,
                            PowerControlState *power_state,
                            char **current_max_file,
                            int current_max_file_num,
                            char **current_limit_file,
                            int current_limit_file_num,
                            const char *normal_current)
{
    restore_charge_calls++;
}

void sync_meizu_wired_level(int is_charging, MeizuWiredLevelState *state)
{
    sync_meizu_calls++;
    sync_meizu_charging = is_charging;
}

void restore_meizu_wired_level(MeizuWiredLevelState *state)
{
    restore_meizu_calls++;
}

void handle_meizu_generation_change(int *last_meizu_thermal_key,
                                    MountModeState *thermal_mount_state,
                                    int is_charging)
{
    meizu_generation_calls++;
}

/* 以下为 temp_simulation.h 的替身实现 */

int read_temp_mc(const char *path, int *out)
{
    read_temp_mc_calls++;

    if (!temp_mc_result) return 0;

    if (out) *out = temp_mc_value;

    return 1;
}

int current_simulated_temp_mc(void)
{
    return simulated_temp_mc;
}

int apply_battery_temp_simulation(TempSimState *st, int is_charging)
{
    apply_temp_simulation_calls++;

    return simulated_temp_mc;
}

int cleanup_battery_temp_simulation(TempSimState *st)
{
    cleanup_temp_simulation_calls++;

    return 0;
}

void handle_option_generation_change(unsigned long *last_generation,
                                     TempSimState *temp_sim_state,
                                     int is_charging)
{
    option_generation_change_calls++;
}
