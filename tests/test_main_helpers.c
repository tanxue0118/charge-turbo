#define _GNU_SOURCE

#include "global.h"
#include "fake_fs.h"
#include "stub_ctrl.h"
#include "stub_modules.h"
#include "stub_options.h"
#include "tc_test.h"

/* main.c 里的检测逻辑全是 static，直接包含实现来测试；
 * Makefile 用 -Dmain=turbo_charge_main 让测试保留自己的入口。 */
#include "main.c"

/* 恢复 main 这个标识符，供本文件自己的入口使用 */
#undef main

#include <pthread.h>

#define BATTERY "/sys/class/power_supply/battery"
#define STATUS_NODE BATTERY "/status"
#define CAPACITY_NODE BATTERY "/capacity"
#define STEP_NODE BATTERY "/step_charging_enabled"

typedef struct {
    uchar battery_status;
    uchar battery_capacity;
    uchar power_control;
    uchar charge_stop_supported;
    uchar step_charge;
    uchar current_change;
    int temp_sensor_num;
    char *temp_sensor;
    char **current_max_file;
    int current_max_file_num;
    char **current_limit_file;
    int current_limit_file_num;
} RequiredFiles;

static void reset_all(void)
{
    fake_fs_reset();
    stub_options_reset();
    stub_modules_reset();
    stub_ctrl_reset();
}

static void init_required(RequiredFiles *r)
{
    memset(r, 0, sizeof(*r));
    r->battery_status = 1;
    r->battery_capacity = 1;
    r->power_control = 1;
    r->charge_stop_supported = 1;
    r->step_charge = 1;
    r->current_change = 1;
}

static void run_check_required_files(RequiredFiles *r)
{
    check_required_files(&r->battery_status,
                         &r->battery_capacity,
                         &r->power_control,
                         &r->charge_stop_supported,
                         &r->step_charge,
                         &r->current_change,
                         &r->temp_sensor_num,
                         &r->temp_sensor,
                         &r->current_max_file,
                         &r->current_max_file_num,
                         &r->current_limit_file,
                         &r->current_limit_file_num);
}

static void free_required(RequiredFiles *r)
{
    free(r->temp_sensor);
    r->temp_sensor = NULL;

    free_string_array(&r->current_max_file, r->current_max_file_num);
    free_string_array(&r->current_limit_file, r->current_limit_file_num);
    r->current_max_file_num = 0;
    r->current_limit_file_num = 0;
}

static void add_thermal_zone(int index, const char *type, const char *temp)
{
    char dir[128];
    char path[160];

    snprintf(dir, sizeof(dir), "/sys/class/thermal/thermal_zone%d", index);
    fake_fs_add_dir(dir);

    if (type) {
        snprintf(path, sizeof(path), "%s/type", dir);
        fake_fs_add_file(path, type);
    }

    if (temp) {
        snprintf(path, sizeof(path), "%s/temp", dir);
        fake_fs_add_file(path, temp);
    }
}

static void test_get_charging_state(void)
{
    TC_ASSERT_INT(get_charging_state("Charging"), 1);
    TC_ASSERT_INT(get_charging_state("Full"), 1);

    /* 插着线但暂停充电也算已连接 */
    TC_ASSERT_INT(get_charging_state("Not charging"), 1);

    TC_ASSERT_INT(get_charging_state("Discharging"), 0);
    TC_ASSERT_INT(get_charging_state("Unknown"), 0);
    TC_ASSERT_INT(get_charging_state(""), 0);
    TC_ASSERT_INT(get_charging_state(NULL), 0);
}

static void test_get_power_connection_state(void)
{
    reset_all();

    /* 外部供电节点可读时以它为准 */
    stub_set_external_power_state(1);
    TC_ASSERT_INT(get_power_connection_state(1, "Discharging"), 1);

    stub_set_external_power_state(0);
    TC_ASSERT_INT(get_power_connection_state(1, "Not charging"), 0);
    TC_ASSERT_INT(get_power_connection_state(0, NULL), 0);

    /* 节点不可读时回退到电池状态 */
    stub_set_external_power_state(-1);
    TC_ASSERT_INT(get_power_connection_state(1, "Charging"), 1);
    TC_ASSERT_INT(get_power_connection_state(1, "Discharging"), 0);

    /* 连电池状态都没有时按已连接处理 */
    TC_ASSERT_INT(get_power_connection_state(0, NULL), 1);
}

static void test_find_temp_sensor_priority(void)
{
    reset_all();

    fake_fs_add_dir("/sys/class/thermal");

    /* 优先级由 temp_sensors 顺序决定，battery 优先于 usb */
    add_thermal_zone(0, "usb", "30000");
    add_thermal_zone(1, "battery", "35000");
    add_thermal_zone(2, "cpu-0-0", "40000");

    char *sensor = NULL;
    TC_ASSERT_INT(find_temp_sensor(&sensor), 1);
    TC_ASSERT_STR(sensor, "/sys/class/thermal/thermal_zone1/temp");
    free(sensor);
}

static void test_find_temp_sensor_skips_invalid(void)
{
    reset_all();

    fake_fs_add_dir("/sys/class/thermal");

    /* 温度为 0/1/-1 的传感器不可信 */
    add_thermal_zone(0, "battery", "0");
    add_thermal_zone(1, "battery-high", "1");
    add_thermal_zone(2, "battery-low", "-1");

    /* 缺少 type 或 temp 的热区被跳过 */
    add_thermal_zone(3, NULL, "36000");
    add_thermal_zone(4, "batt_therm", NULL);

    /* 非 thermal_zone 目录被跳过 */
    fake_fs_add_dir("/sys/class/thermal/cooling_device0");
    fake_fs_add_file("/sys/class/thermal/cooling_device0/type", "battery");
    fake_fs_add_file("/sys/class/thermal/cooling_device0/temp", "36000");

    char *sensor = NULL;
    TC_ASSERT_INT(find_temp_sensor(&sensor), 0);
    TC_ASSERT(sensor == NULL);

    /* 之后出现可用传感器时才成功 */
    add_thermal_zone(5, "battery_therm", "36000");
    TC_ASSERT_INT(find_temp_sensor(&sensor), 1);
    TC_ASSERT_STR(sensor, "/sys/class/thermal/thermal_zone5/temp");
    free(sensor);

    TC_ASSERT_INT(find_temp_sensor(NULL), 0);
}

static void test_find_temp_sensor_no_thermal_dir(void)
{
    reset_all();

    char *sensor = NULL;
    TC_ASSERT_INT(find_temp_sensor(&sensor), 0);
    TC_ASSERT(sensor == NULL);
}

static void test_check_required_files_all_missing(void)
{
    reset_all();

    RequiredFiles r;
    init_required(&r);

    run_check_required_files(&r);

    /* 什么节点都没有时各项功能逐个失效 */
    TC_ASSERT_INT(r.battery_status, 0);
    TC_ASSERT_INT(r.battery_capacity, 0);
    TC_ASSERT_INT(r.power_control, 0);
    TC_ASSERT_INT(r.charge_stop_supported, 0);
    TC_ASSERT_INT(r.step_charge, 0);
    TC_ASSERT_INT(r.current_change, 0);
    TC_ASSERT_INT(r.current_max_file_num, 0);
    TC_ASSERT_INT(r.current_limit_file_num, 0);
    TC_ASSERT_INT(r.temp_sensor_num, 0);
    TC_ASSERT(r.temp_sensor == NULL);

    free_required(&r);
}

static void test_check_required_files_full_support(void)
{
    reset_all();

    fake_fs_add_dir(BATTERY);
    fake_fs_add_file(STATUS_NODE, "Charging");
    fake_fs_add_file(CAPACITY_NODE, "80");
    fake_fs_add_file(BATTERY "/charging_enabled", "1");
    fake_fs_add_file(STEP_NODE, "1");

    fake_fs_add_dir("/sys/class/power_supply");
    fake_fs_add_file(BATTERY "/constant_charge_current_max", "50000000");
    fake_fs_add_file(BATTERY "/input_current_limit", "-1");

    fake_fs_add_dir("/sys/class/thermal");
    add_thermal_zone(0, "battery", "35000");

    RequiredFiles r;
    init_required(&r);

    run_check_required_files(&r);

    TC_ASSERT_INT(r.battery_status, 1);
    TC_ASSERT_INT(r.battery_capacity, 1);
    TC_ASSERT_INT(r.power_control, 1);
    TC_ASSERT_INT(r.charge_stop_supported, 1);
    TC_ASSERT_INT(r.step_charge, 1);
    TC_ASSERT_INT(r.current_change, 1);

    TC_ASSERT_INT(r.current_max_file_num, 1);
    TC_ASSERT_STR(r.current_max_file[0], BATTERY "/constant_charge_current_max");
    TC_ASSERT_INT(r.current_limit_file_num, 1);
    TC_ASSERT_STR(r.current_limit_file[0], BATTERY "/input_current_limit");

    TC_ASSERT_INT(r.temp_sensor_num, 1);
    TC_ASSERT_STR(r.temp_sensor, "/sys/class/thermal/thermal_zone0/temp");

    free_required(&r);
}

static void test_check_required_files_stop_control_variants(void)
{
    const char *nodes[] = {
        BATTERY "/battery_charging_enabled",
        BATTERY "/input_suspend",
        "/sys/class/qcom-battery/restricted_charging",
        "/sys/class/qcom-battery/restrict_chg",
    };

    /* 任意一个暂停充电节点存在即视为支持 */
    for (size_t i = 0; i < sizeof(nodes) / sizeof(nodes[0]); i++) {
        reset_all();

        RequiredFiles r;
        init_required(&r);

        fake_fs_add_file(nodes[i], "0");
        run_check_required_files(&r);

        TC_ASSERT_INT(r.charge_stop_supported, 1);

        free_required(&r);
    }
}

static void test_check_required_files_step_charge_without_capacity(void)
{
    reset_all();

    RequiredFiles r;
    init_required(&r);

    fake_fs_add_file(STATUS_NODE, "Charging");
    fake_fs_add_file(STEP_NODE, "1");

    run_check_required_files(&r);

    /* 有阶梯充电节点但没有电量节点时降级为不看电量 */
    TC_ASSERT_INT(r.battery_capacity, 0);
    TC_ASSERT_INT(r.step_charge, 2);
    TC_ASSERT_INT(r.power_control, 0);

    free_required(&r);
}

static void test_check_required_files_limit_only(void)
{
    reset_all();

    RequiredFiles r;
    init_required(&r);

    fake_fs_add_file(CAPACITY_NODE, "80");
    fake_fs_add_dir("/sys/class/power_supply");
    fake_fs_add_dir("/sys/class/power_supply/usb");
    fake_fs_add_file("/sys/class/power_supply/usb/input_current_max", "-1");

    run_check_required_files(&r);

    /* 只有限流文件时电流控制仍然可用 */
    TC_ASSERT_INT(r.current_change, 1);
    TC_ASSERT_INT(r.current_max_file_num, 0);
    TC_ASSERT_INT(r.current_limit_file_num, 1);

    /* 没有可用温度传感器 */
    TC_ASSERT_INT(r.temp_sensor_num, 0);

    free_required(&r);
}

static void test_check_required_files_collects_all_current_nodes(void)
{
    reset_all();

    RequiredFiles r;
    init_required(&r);

    fake_fs_add_dir("/sys/class/power_supply");
    fake_fs_add_dir(BATTERY);
    fake_fs_add_file(BATTERY "/constant_charge_current", "1");
    fake_fs_add_file(BATTERY "/fast_charge_current", "1");
    fake_fs_add_file(BATTERY "/thermal_input_current", "1");
    fake_fs_add_file(BATTERY "/thermal_input_current_limit", "1");
    fake_fs_add_file(BATTERY "/input_current_limit", "1");
    fake_fs_add_file(BATTERY "/input_current_max", "1");

    /* 不相关的节点不会被收集 */
    fake_fs_add_file(BATTERY "/voltage_now", "4000");

    run_check_required_files(&r);

    TC_ASSERT_INT(r.current_max_file_num, 3);
    TC_ASSERT_INT(r.current_limit_file_num, 3);

    free_required(&r);
}

static void *main_thread_func(void *arg)
{
    turbo_charge_main();

    return NULL;
}

static void test_main_loop_runs_and_cleans_up(void)
{
    reset_all();

    fake_fs_add_file(STATUS_NODE, "Discharging");
    fake_fs_add_file(CAPACITY_NODE, "50");
    fake_fs_add_file(STEP_NODE, "1");
    fake_fs_add_file(BATTERY "/input_suspend", "0");

    stub_options_set("CYCLE_TIME", 1);
    stub_options_set("CURRENT_MAX", 50000000);
    stub_set_external_power_state(0);

    program_running = 1;

    pthread_t tid;
    TC_ASSERT_INT(pthread_create(&tid, NULL, main_thread_func, NULL), 0);

    /* 未连接充电器时主循环只做检测与同步 */
    for (int i = 0; i < 500 && stub_sync_bypass_calls() == 0; i++) {
        usleep(10 * 1000);
    }

    TC_ASSERT(stub_sync_bypass_calls() >= 1);
    TC_ASSERT(stub_power_ctl_calls() >= 1);
    TC_ASSERT(stub_sync_thermal_calls() >= 1);
    TC_ASSERT_INT(stub_last_sync_meizu_charging(), 0);
    TC_ASSERT_INT(stub_bypass_charge_ctl_calls(), 0);
    TC_ASSERT_STR(stub_last_sync_bypass_current(), "50000000");

    /* 启动时打开充电并把限流文件恢复到 -1 */
    TC_ASSERT_STR(stub_last_charge_ctl_value(), "1");

    program_running = 0;

    void *ret = NULL;
    TC_ASSERT_INT(pthread_join(tid, &ret), 0);

    /* 退出时清理所有控制状态 */
    TC_ASSERT_INT(stub_restore_charge_calls(), 1);
    TC_ASSERT_INT(stub_restore_meizu_wired_calls(), 1);
    TC_ASSERT_INT(stub_cleanup_temp_simulation_calls(), 1);
    TC_ASSERT_INT(stub_umount_thermal_calls(), 1);
    TC_ASSERT(stub_foreground_stop_calls() >= 1);

    program_running = 1;
}

/* 让主循环进入充电分支所需的节点与选项 */
static void prepare_charging_environment(void)
{
    reset_all();

    fake_fs_add_file(STATUS_NODE, "Charging");
    fake_fs_add_file(CAPACITY_NODE, "50");
    fake_fs_add_file(STEP_NODE, "1");
    fake_fs_add_file(BATTERY "/input_suspend", "0");

    fake_fs_add_dir("/sys/class/power_supply");
    fake_fs_add_dir(BATTERY);
    fake_fs_add_file(BATTERY "/constant_charge_current_max", "50000000");
    fake_fs_add_file(BATTERY "/input_current_limit", "-1");

    fake_fs_add_dir("/sys/class/thermal");
    add_thermal_zone(0, "battery", "35000");

    stub_options_set("CYCLE_TIME", 1);
    stub_options_set("CURRENT_MAX", 50000000);
    stub_options_set("TEMP_CTRL", 1);
    stub_options_set("TEMP_LEVEL1", 45);
    stub_options_set("TEMP_LEVEL2", 50);
    stub_options_set("TEMP_MAX", 52);
    stub_options_set("TEMP_LEVEL1_CURRENT", 2000000);
    stub_options_set("TEMP_LEVEL2_CURRENT", 1000000);

    stub_set_external_power_state(1);
}

/* 等待条件成立，最多约 10 秒 */
#define WAIT_FOR(cond)                                                       \
    do {                                                                     \
        int tc_i = 0;                                                        \
        for (; tc_i < 1000 && !(cond); tc_i++) usleep(10 * 1000);            \
        TC_ASSERT(cond);                                                     \
    } while (0)

static void test_main_loop_bypass_skips_temp_control(void)
{
    prepare_charging_environment();
    stub_set_bypass_active(1);
    stub_set_bypass_charge_ctl_result(1, 0);

    program_running = 1;

    pthread_t tid;
    TC_ASSERT_INT(pthread_create(&tid, NULL, main_thread_func, NULL), 0);

    /* 充电时才请求应用旁路，旁路生效后跳过温控 */
    WAIT_FOR(stub_bypass_charge_ctl_calls() >= 1);
    TC_ASSERT_INT(stub_last_sync_bypass_requested(), 1);
    TC_ASSERT_INT(stub_read_temp_mc_calls(), 0);

    program_running = 0;
    TC_ASSERT_INT(pthread_join(tid, NULL), 0);

    program_running = 1;
}

static void test_main_loop_temp_levels(void)
{
    prepare_charging_environment();

    /* 第二档温度：限流到 TEMP_LEVEL2_CURRENT */
    stub_set_simulated_temp_mc(51000);

    program_running = 1;

    pthread_t tid;
    TC_ASSERT_INT(pthread_create(&tid, NULL, main_thread_func, NULL), 0);

    WAIT_FOR(strcmp(stub_last_set_value(BATTERY "/constant_charge_current_max"),
                    "1000000") == 0);

    /* 第一档温度：限流到 TEMP_LEVEL1_CURRENT */
    stub_set_simulated_temp_mc(46000);
    WAIT_FOR(strcmp(stub_last_set_value(BATTERY "/constant_charge_current_max"),
                    "2000000") == 0);

    /* 降到第一档以下时退出温控循环并恢复设定电流 */
    stub_set_simulated_temp_mc(30000);
    WAIT_FOR(strcmp(stub_last_set_value(BATTERY "/constant_charge_current_max"),
                    "50000000") == 0);

    program_running = 0;
    TC_ASSERT_INT(pthread_join(tid, NULL), 0);

    program_running = 1;
}

static void test_main_loop_temp_max_stops_charging(void)
{
    prepare_charging_environment();

    /* 超过第三档：停止充电并等待降温 */
    stub_set_simulated_temp_mc(60000);

    program_running = 1;

    pthread_t tid;
    TC_ASSERT_INT(pthread_create(&tid, NULL, main_thread_func, NULL), 0);

    WAIT_FOR(strcmp(stub_last_charge_ctl_value(), "0") == 0);

    /* 降温后恢复充电 */
    stub_set_simulated_temp_mc(30000);
    WAIT_FOR(strcmp(stub_last_charge_ctl_value(), "1") == 0);

    program_running = 0;
    TC_ASSERT_INT(pthread_join(tid, NULL), 0);

    program_running = 1;
}

static void test_main_loop_without_current_max_files(void)
{
    reset_all();

    fake_fs_add_file(STATUS_NODE, "Charging");
    fake_fs_add_file(STEP_NODE, "1");
    fake_fs_add_dir("/sys/class/power_supply");
    fake_fs_add_dir(BATTERY);
    fake_fs_add_file(BATTERY "/input_current_limit", "-1");

    fake_fs_add_dir("/sys/class/thermal");
    add_thermal_zone(0, "battery", "35000");

    stub_options_set("CYCLE_TIME", 1);
    stub_options_set("CURRENT_MAX", 40000000);
    stub_options_set("TEMP_CTRL", 1);
    stub_options_set("TEMP_LEVEL1", 45);
    stub_options_set("TEMP_LEVEL2", 50);
    stub_options_set("TEMP_MAX", 52);
    stub_options_set("TEMP_LEVEL1_CURRENT", 2000000);
    stub_set_external_power_state(1);
    stub_set_simulated_temp_mc(46000);

    program_running = 1;

    pthread_t tid;
    TC_ASSERT_INT(pthread_create(&tid, NULL, main_thread_func, NULL), 0);

    /* 没有电流控制文件时退化为使用限流文件 */
    WAIT_FOR(strcmp(stub_last_set_value(BATTERY "/input_current_limit"),
                    "2000000") == 0);

    /* 没有电量节点时按电量控制被禁用 */
    TC_ASSERT_INT(stub_last_power_ctl_capacity_available(), 0);
    TC_ASSERT_STR(stub_last_apply_step_charge_power(), "0");

    stub_set_simulated_temp_mc(20000);
    WAIT_FOR(strcmp(stub_last_set_value(BATTERY "/input_current_limit"), "-1") == 0);

    program_running = 0;
    TC_ASSERT_INT(pthread_join(tid, NULL), 0);

    program_running = 1;
}

static void test_main_loop_disconnect_during_temp_control(void)
{
    prepare_charging_environment();
    stub_set_simulated_temp_mc(46000);

    program_running = 1;

    pthread_t tid;
    TC_ASSERT_INT(pthread_create(&tid, NULL, main_thread_func, NULL), 0);

    WAIT_FOR(strcmp(stub_last_set_value(BATTERY "/constant_charge_current_max"),
                    "2000000") == 0);

    /* 温控循环中断开充电器：退出循环并恢复控制 */
    stub_set_external_power_state(0);
    WAIT_FOR(stub_last_sync_meizu_charging() == 0);
    TC_ASSERT(stub_foreground_stop_calls() >= 1);

    program_running = 0;
    TC_ASSERT_INT(pthread_join(tid, NULL), 0);

    program_running = 1;
}

int main(void)
{
    TC_RUN(test_get_charging_state);
    TC_RUN(test_get_power_connection_state);
    TC_RUN(test_find_temp_sensor_priority);
    TC_RUN(test_find_temp_sensor_skips_invalid);
    TC_RUN(test_find_temp_sensor_no_thermal_dir);
    TC_RUN(test_check_required_files_all_missing);
    TC_RUN(test_check_required_files_full_support);
    TC_RUN(test_check_required_files_stop_control_variants);
    TC_RUN(test_check_required_files_step_charge_without_capacity);
    TC_RUN(test_check_required_files_limit_only);
    TC_RUN(test_check_required_files_collects_all_current_nodes);
    TC_RUN(test_main_loop_runs_and_cleans_up);
    TC_RUN(test_main_loop_bypass_skips_temp_control);
    TC_RUN(test_main_loop_temp_levels);
    TC_RUN(test_main_loop_temp_max_stops_charging);
    TC_RUN(test_main_loop_without_current_max_files);
    TC_RUN(test_main_loop_disconnect_during_temp_control);

    return tc_report("main_helpers");
}
