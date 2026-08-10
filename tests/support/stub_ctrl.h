#ifndef TURBO_CHARGE_TEST_STUB_CTRL_H
#define TURBO_CHARGE_TEST_STUB_CTRL_H

/* some_ctrl / temp_simulation 的替身与调用记录 */

void stub_ctrl_reset(void);

/* some_ctrl */
void stub_set_external_power_state(int state);
int stub_read_external_power_calls(void);
const char *stub_last_charge_ctl_value(void);
int stub_charge_ctl_calls(void);
int stub_step_charge_ctl_calls(void);
void stub_set_power_ctl_result(int stop_requested, int bypass_requested);
int stub_power_ctl_calls(void);
int stub_last_power_ctl_capacity_available(void);
int stub_last_power_ctl_stop_supported(void);
void stub_set_bypass_active(int active);
int stub_sync_bypass_calls(void);
int stub_last_sync_bypass_stop_requested(void);
int stub_last_sync_bypass_requested(void);
const char *stub_last_sync_bypass_current(void);
int stub_restore_charge_calls(void);
int stub_bypass_charge_ctl_calls(void);
void stub_set_bypass_charge_ctl_result(int app_bypass_requested, int screen_is_off);
int stub_sync_meizu_wired_calls(void);
int stub_last_sync_meizu_charging(void);
int stub_restore_meizu_wired_calls(void);
int stub_meizu_generation_calls(void);
int stub_apply_step_charge_calls(void);
const char *stub_last_apply_step_charge_power(void);

/* temp_simulation */
void stub_set_temp_mc(int temp_mc, int result);
int stub_read_temp_mc_calls(void);
void stub_set_simulated_temp_mc(int temp_mc);
int stub_apply_temp_simulation_calls(void);
int stub_cleanup_temp_simulation_calls(void);
int stub_option_generation_change_calls(void);

#endif
