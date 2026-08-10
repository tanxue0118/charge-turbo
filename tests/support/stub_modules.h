#ifndef TURBO_CHARGE_TEST_STUB_MODULES_H
#define TURBO_CHARGE_TEST_STUB_MODULES_H

/* value_set / thermal_mount / foreground_app 的替身与调用记录 */

void stub_modules_reset(void);

/* value_set */
int stub_set_value_count(const char *path);
const char *stub_last_set_value(const char *path);
int stub_set_value_total(void);
void stub_set_meizu_write_result(int result, int found, int success);
int stub_meizu_write_calls(void);
int stub_meizu_last_level(void);
void stub_set_meizu_restore_result(int result);
int stub_meizu_restore_calls(void);

/* thermal_mount */
int stub_mount_thermal_calls(void);
int stub_umount_thermal_calls(void);
int stub_sync_thermal_calls(void);

/* foreground_app */
void stub_set_foreground_app_name(const char *name);
void stub_set_bypass_app_list(const char **apps, int count, int result);
int stub_foreground_start_calls(void);
int stub_foreground_stop_calls(void);
void stub_set_android_version(int version);

#endif
