#define _GNU_SOURCE

#include "global.h"
#include "tc_test.h"

#include <string.h>

static void test_clamp_meizu_charge_level(void)
{
    TC_ASSERT_INT(clamp_meizu_charge_level(1), 1);
    TC_ASSERT_INT(clamp_meizu_charge_level(5), 5);
    TC_ASSERT_INT(clamp_meizu_charge_level(10), 10);

    /* 越界值统一回落到默认档位 10 */
    TC_ASSERT_INT(clamp_meizu_charge_level(0), 10);
    TC_ASSERT_INT(clamp_meizu_charge_level(-3), 10);
    TC_ASSERT_INT(clamp_meizu_charge_level(11), 10);
    TC_ASSERT_INT(clamp_meizu_charge_level(9999), 10);
}

static void test_clamp_meizu_thermal_scheme(void)
{
    TC_ASSERT_INT(clamp_meizu_thermal_scheme(MEIZU_THERMAL_SCHEME_FLYME_CLEAR),
                  MEIZU_THERMAL_SCHEME_FLYME_CLEAR);
    TC_ASSERT_INT(clamp_meizu_thermal_scheme(MEIZU_THERMAL_SCHEME_EXTREMEGT),
                  MEIZU_THERMAL_SCHEME_EXTREMEGT);

    TC_ASSERT_INT(clamp_meizu_thermal_scheme(0), MEIZU_THERMAL_SCHEME_EXTREMEGT);
    TC_ASSERT_INT(clamp_meizu_thermal_scheme(3), MEIZU_THERMAL_SCHEME_EXTREMEGT);
    TC_ASSERT_INT(clamp_meizu_thermal_scheme(-1), MEIZU_THERMAL_SCHEME_EXTREMEGT);
}

static void test_handle_exit_signal(void)
{
    program_running = 1;
    handle_exit_signal(SIGTERM);
    TC_ASSERT_INT(program_running, 0);

    program_running = 1;
    handle_exit_signal(SIGINT);
    TC_ASSERT_INT(program_running, 0);

    program_running = 1;
}

static void test_option_table_is_consistent(void)
{
    TC_ASSERT(option_count > 0);

    for (int i = 0; i < option_count; i++) {
        TC_ASSERT(options[i].name != NULL);
        TC_ASSERT(options[i].name[0] != '\0');
        TC_ASSERT(strlen(options[i].name) < OPTION_NAME_MAX_SIZE);

        /* 初始值必须等于默认值，否则运行时无法判断“沿用上一次的值” */
        TC_ASSERT_INT(options[i].value, options[i].default_value);
        TC_ASSERT(options[i].default_value >= 0);

        for (int j = i + 1; j < option_count; j++) {
            if (strcmp(options[i].name, options[j].name) == 0) {
                TC_FAIL("duplicated option name %s", options[i].name);
            }
        }
    }
}

static void test_option_defaults_are_sane(void)
{
    int cycle_time = -1;
    int charge_stop = -1;
    int charge_start = -1;
    int temp_level1 = -1;
    int temp_level2 = -1;
    int temp_max = -1;

    for (int i = 0; i < option_count; i++) {
        const char *name = options[i].name;
        int value = options[i].default_value;

        if (!strcmp(name, "CYCLE_TIME")) cycle_time = value;
        else if (!strcmp(name, "CHARGE_STOP")) charge_stop = value;
        else if (!strcmp(name, "CHARGE_START")) charge_start = value;
        else if (!strcmp(name, "TEMP_LEVEL1")) temp_level1 = value;
        else if (!strcmp(name, "TEMP_LEVEL2")) temp_level2 = value;
        else if (!strcmp(name, "TEMP_MAX")) temp_max = value;
    }

    TC_ASSERT(cycle_time > 0);
    TC_ASSERT(charge_stop > 0 && charge_stop <= 100);
    TC_ASSERT(charge_start >= 0 && charge_start < charge_stop);
    TC_ASSERT(temp_level1 < temp_level2);
    TC_ASSERT(temp_level2 < temp_max);
}

static void test_temp_sensor_table(void)
{
    TC_ASSERT(temp_sensor_count > 0);

    for (int i = 0; i < temp_sensor_count; i++) {
        TC_ASSERT(temp_sensors[i] != NULL);
        TC_ASSERT(temp_sensors[i][0] != '\0');
    }

    /* 电池相关传感器优先级必须高于外壳/USB 传感器 */
    TC_ASSERT_STR(temp_sensors[0], "battery");
}

static void test_state_initializers(void)
{
    PowerControlState power = POWER_CONTROL_STATE_INITIALIZER;
    TC_ASSERT_INT(power.last_charge_stop, -1);
    TC_ASSERT_INT(power.last_mode, -1);
    TC_ASSERT_INT(power.active, 0);
    TC_ASSERT_INT(power.stop_applied, 0);

    BypassState bypass = BYPASS_STATE_INITIALIZER;
    TC_ASSERT_INT(bypass.mode, BYPASS_MODE_OFF);
    TC_ASSERT_INT(bypass.node_path[0], 0);
    TC_ASSERT_INT(bypass.next_probe_time, 0);

    MeizuWiredLevelState meizu = MEIZU_WIRED_LEVEL_STATE_INITIALIZER;
    TC_ASSERT_INT(meizu.mode, MEIZU_WIRED_LEVEL_MODE_UNKNOWN);
    TC_ASSERT_INT(meizu.last_attempted_level, MEIZU_LEVEL_INACTIVE);
    TC_ASSERT_INT(meizu.last_write_result, MEIZU_LEVEL_INACTIVE);
}

int main(void)
{
    TC_RUN(test_clamp_meizu_charge_level);
    TC_RUN(test_clamp_meizu_thermal_scheme);
    TC_RUN(test_handle_exit_signal);
    TC_RUN(test_option_table_is_consistent);
    TC_RUN(test_option_defaults_are_sane);
    TC_RUN(test_temp_sensor_table);
    TC_RUN(test_state_initializers);

    return tc_report("global");
}
