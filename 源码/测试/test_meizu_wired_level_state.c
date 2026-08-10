#include "some_ctrl.h"
#include "read_option_file.h"
#include "printf_with_time.h"
#include "value_set.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int meizu_device;
static int meizu_level;
static int write_result;
static int write_calls;
static int restore_calls;
static int restore_result;
static int disabled_logs;
static int not_charging_logs;
static int prepare_logs;
static int missing_logs;
static int failure_logs;

static void reset_fakes(void)
{
    meizu_device = 0;
    meizu_level = 10;
    write_result = 0;
    write_calls = 0;
    restore_calls = 0;
    restore_result = 0;
    disabled_logs = 0;
    not_charging_logs = 0;
    prepare_logs = 0;
    missing_logs = 0;
    failure_logs = 0;
}

static MeizuWiredLevelState new_state(void)
{
    MeizuWiredLevelState state = MEIZU_WIRED_LEVEL_STATE_INITIALIZER;
    return state;
}

int read_one_option(const char *name)
{
    if (strcmp(name, "MEIZU_DEVICE") == 0) return meizu_device;
    if (strcmp(name, "MEIZU_CHARGE_LEVEL") == 0) return meizu_level;
    return 0;
}

int clamp_meizu_charge_level(int level)
{
    if (level < 1 || level > 10) return 10;
    return level;
}

void printf_with_time(const char *format, ...)
{
    if (strstr(format, "魅族适配未开启")) disabled_logs++;
    if (strstr(format, "当前未在充电")) not_charging_logs++;
    if (strstr(format, "准备写入魅族充电档位")) prepare_logs++;
    if (strstr(format, "nodes missing")) missing_logs++;
    if (strstr(format, "魅族充电档位写入失败")) failure_logs++;
}

int write_meizu_wired_level_with_echo(int level, int *found_count, int *success_count)
{
    (void)level;
    write_calls++;
    if (found_count) *found_count = write_result == MEIZU_LEVEL_NODE_MISSING ? 0 : 1;
    if (success_count) *success_count = write_result == 0 ? 1 : 0;
    return write_result;
}

int restore_meizu_wired_level_permission(void)
{
    restore_calls++;
    return restore_result;
}

int clamp_meizu_thermal_scheme(int scheme)
{
    return scheme;
}

const char *meizu_thermal_scheme_name(int scheme)
{
    (void)scheme;
    return "test";
}

void umount_thermal_files(void)
{
}

void sync_thermal_mount_mode(int is_charging, MountModeState *state)
{
    (void)is_charging;
    (void)state;
}

void set_value(const char *file, const char *value)
{
    (void)file;
    (void)value;
}

void set_array_value(char **files, int num, const char *value)
{
    (void)files;
    (void)num;
    (void)value;
}

int read_file(const char *file_path, char *buf, size_t buf_size)
{
    (void)file_path;
    (void)buf;
    (void)buf_size;
    return 0;
}

void stop_foreground_thread(void)
{
}

void start_foreground_thread_if_needed(int android_version)
{
    (void)android_version;
}

void get_foreground_app(char *out, size_t out_size)
{
    if (out && out_size > 0) out[0] = '\0';
}

int load_bypass_app_list(char ***apps, int *app_num, time_t *last_mtime)
{
    (void)apps;
    (void)app_num;
    (void)last_mtime;
    return 0;
}

static void test_disabled_logs_once(void)
{
    reset_fakes();
    MeizuWiredLevelState state = new_state();

    sync_meizu_wired_level(1, &state);
    sync_meizu_wired_level(1, &state);

    assert(disabled_logs == 1);
    assert(write_calls == 0);
    assert(restore_calls == 0);
}

static void test_not_charging_logs_once(void)
{
    reset_fakes();
    meizu_device = 1;
    MeizuWiredLevelState state = new_state();

    sync_meizu_wired_level(0, &state);
    sync_meizu_wired_level(0, &state);

    assert(not_charging_logs == 1);
    assert(write_calls == 0);
    assert(restore_calls == 0);
}

static void test_success_is_not_rewritten_until_level_changes(void)
{
    reset_fakes();
    meizu_device = 1;
    meizu_level = 7;
    MeizuWiredLevelState state = new_state();

    sync_meizu_wired_level(1, &state);
    sync_meizu_wired_level(1, &state);
    assert(write_calls == 1);
    assert(prepare_logs == 1);

    meizu_level = 8;
    sync_meizu_wired_level(1, &state);
    sync_meizu_wired_level(1, &state);
    assert(write_calls == 2);
    assert(prepare_logs == 2);
}

static void test_failure_is_not_retried_until_new_opportunity(void)
{
    reset_fakes();
    meizu_device = 1;
    meizu_level = 6;
    write_result = -9;
    MeizuWiredLevelState state = new_state();

    sync_meizu_wired_level(1, &state);
    sync_meizu_wired_level(1, &state);
    assert(write_calls == 1);
    assert(failure_logs == 1);

    meizu_level = 5;
    sync_meizu_wired_level(1, &state);
    assert(write_calls == 2);

    sync_meizu_wired_level(0, &state);
    sync_meizu_wired_level(0, &state);
    assert(restore_calls == 1);

    sync_meizu_wired_level(1, &state);
    assert(write_calls == 3);
}

static void test_missing_node_is_not_retried_or_restored(void)
{
    reset_fakes();
    meizu_device = 1;
    write_result = MEIZU_LEVEL_NODE_MISSING;
    MeizuWiredLevelState state = new_state();

    sync_meizu_wired_level(1, &state);
    sync_meizu_wired_level(1, &state);
    assert(write_calls == 1);
    assert(missing_logs == 1);

    sync_meizu_wired_level(0, &state);
    assert(restore_calls == 0);

    sync_meizu_wired_level(1, &state);
    assert(write_calls == 2);
}

static void test_reenable_after_disabled_allows_one_new_attempt(void)
{
    reset_fakes();
    meizu_device = 1;
    meizu_level = 4;
    MeizuWiredLevelState state = new_state();

    sync_meizu_wired_level(1, &state);
    assert(write_calls == 1);

    meizu_device = 0;
    sync_meizu_wired_level(1, &state);
    sync_meizu_wired_level(1, &state);
    assert(disabled_logs == 1);
    assert(restore_calls == 1);

    meizu_device = 1;
    sync_meizu_wired_level(1, &state);
    sync_meizu_wired_level(1, &state);
    assert(write_calls == 2);
}

static void test_restore_failure_is_not_retried(void)
{
    reset_fakes();
    meizu_device = 1;
    restore_result = -5;
    MeizuWiredLevelState state = new_state();

    sync_meizu_wired_level(1, &state);
    restore_meizu_wired_level(&state);
    restore_meizu_wired_level(&state);
    assert(restore_calls == 1);
}

static void test_restore_runs_once_for_success_and_never_for_untouched_state(void)
{
    reset_fakes();
    meizu_device = 1;
    MeizuWiredLevelState state = new_state();

    sync_meizu_wired_level(1, &state);
    restore_meizu_wired_level(&state);
    restore_meizu_wired_level(&state);
    assert(restore_calls == 1);

    MeizuWiredLevelState untouched = new_state();
    restore_meizu_wired_level(&untouched);
    assert(restore_calls == 1);
}

int main(void)
{
    test_disabled_logs_once();
    test_not_charging_logs_once();
    test_success_is_not_rewritten_until_level_changes();
    test_failure_is_not_retried_until_new_opportunity();
    test_missing_node_is_not_retried_or_restored();
    test_reenable_after_disabled_allows_one_new_attempt();
    test_restore_failure_is_not_retried();
    test_restore_runs_once_for_success_and_never_for_untouched_state();
    puts("meizu wired-level state tests passed");
    return 0;
}
