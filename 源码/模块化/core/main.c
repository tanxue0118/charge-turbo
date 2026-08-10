#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "foreground_app.h"
#include "main.h"
#include "printf_with_time.h"
#include "read_option_file.h"
#include "some_ctrl.h"
#include "str_array.h"
#include "temp_simulation.h"
#include "thermal_mount.h"
#include "value_set.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int find_temp_sensor(char **temp_sensor)
{
    if (!temp_sensor) return 0;

    *temp_sensor = NULL;

    char **thermal_dirs = NULL;
    int thermal_num = list_dir(THERMAL_DIR, &thermal_dirs);

    int best_index = 9999;
    char best_path[PATH_MAX] = {0};

    for (int i = 0; i < thermal_num; i++) {
        if (!thermal_dirs[i]) continue;
        if (!strstr(thermal_dirs[i], "thermal_zone")) continue;

        char type_path[PATH_MAX] = {0};
        char temp_path[PATH_MAX] = {0};
        char type[64] = {0};
        char temp[32] = {0};

        join_path(type_path, sizeof(type_path), thermal_dirs[i], "type");
        join_path(temp_path, sizeof(temp_path), thermal_dirs[i], "temp");

        if (!read_file(type_path, type, sizeof(type))) continue;
        if (!read_file(temp_path, temp, sizeof(temp))) continue;

        int temp_int = atoi(temp);

        if (temp_int == 0 || temp_int == 1 || temp_int == -1) continue;

        for (int j = 0; j < temp_sensor_count; j++) {
            if (strcmp(type, temp_sensors[j]) == 0 && j < best_index) {
                best_index = j;
                snprintf(best_path, sizeof(best_path), "%s", temp_path);
            }
        }
    }

    free_string_array(&thermal_dirs, thermal_num);

    if (best_index != 9999 && best_path[0]) {
        *temp_sensor = strdup(best_path);
        if (!*temp_sensor) return 0;

        printf_with_time("将使用 %s 温度传感器作为手机温度获取源", temp_sensors[best_index]);
        printf_with_time("温度传感器路径：%s", *temp_sensor);

        return 1;
    }

    return 0;
}

static const char *const CHARGE_CURRENT_MAX_SUFFIXES[] = {
    "/constant_charge_current_max",
    "/constant_charge_current",
    "/fast_charge_current",
    "/thermal_input_current"
};

static const char *const CHARGE_CURRENT_LIMIT_SUFFIXES[] = {
    "/thermal_input_current_limit",
    "/input_current_limit",
    "/input_current_max"
};

static int matches_any_suffix(const char *path, const char *const *suffixes, size_t num)
{
    for (size_t i = 0; i < num; i++) {
        if (ends_with(path, suffixes[i])) return 1;
    }

    return 0;
}

static int any_file_exists(const char *const *paths, size_t num)
{
    for (size_t i = 0; i < num; i++) {
        if (file_exists(paths[i])) return 1;
    }

    return 0;
}

static void check_required_files(uchar *battery_status,
                                 uchar *battery_capacity,
                                 uchar *power_control,
                                 uchar *charge_stop_supported,
                                 uchar *step_charge,
                                 uchar *current_change,
                                 int *temp_sensor_num,
                                 char **temp_sensor,
                                 ChargeCurrentNodes *nodes)
{
    if (!file_exists(BATTERY_STATUS_PATH)) {
        *battery_status = 0;
    }

    if (!file_exists(BATTERY_CAPACITY_PATH)) {
        *battery_capacity = 0;
    }

    if (!*battery_capacity) {
        *power_control = 0;
        printf_with_time("找不到 battery/capacity，按电量阈值控制功能失效");
    }

    if (!*battery_status) {
        printf_with_time("找不到 battery/status，将尝试通过外部供电节点判断充电器连接状态");
    }

    static const char *const charge_stop_nodes[] = {
        BATTERY_SUPPLY_DIR "/charging_enabled",
        BATTERY_SUPPLY_DIR "/battery_charging_enabled",
        BATTERY_SUPPLY_DIR "/input_suspend",
        "/sys/class/qcom-battery/restricted_charging",
        "/sys/class/qcom-battery/restrict_chg"
    };

    if (!any_file_exists(charge_stop_nodes,
                         sizeof(charge_stop_nodes) / sizeof(charge_stop_nodes[0]))) {
        *charge_stop_supported = 0;
        printf_with_time("找不到暂停充电控制文件，电量控制的停止充电模式不可用，旁路供电模式仍会尝试运行");
    }

    if (!file_exists(BATTERY_SUPPLY_DIR "/step_charging_enabled")) {
        *step_charge = 0;
        printf_with_time("找不到 step_charging_enabled，阶梯式充电控制失效");
    } else if (!*battery_capacity) {
        *step_charge = 2;
        printf_with_time("找不到 capacity，阶梯式充电无法根据电量控制");
    }

    char **power_supply_dirs = NULL;
    int power_supply_num = list_dir(POWER_SUPPLY_DIR, &power_supply_dirs);

    StrArray max_files;
    StrArray limit_files;
    str_array_init(&max_files);
    str_array_init(&limit_files);

    for (int i = 0; i < power_supply_num; i++) {
        char **files = NULL;
        int file_num = list_dir(power_supply_dirs[i], &files);

        for (int j = 0; j < file_num; j++) {
            char *path = files[j];
            if (!path) continue;

            if (matches_any_suffix(path, CHARGE_CURRENT_MAX_SUFFIXES,
                                   sizeof(CHARGE_CURRENT_MAX_SUFFIXES) /
                                   sizeof(CHARGE_CURRENT_MAX_SUFFIXES[0]))) {
                str_array_push(&max_files, path);
            } else if (matches_any_suffix(path, CHARGE_CURRENT_LIMIT_SUFFIXES,
                                          sizeof(CHARGE_CURRENT_LIMIT_SUFFIXES) /
                                          sizeof(CHARGE_CURRENT_LIMIT_SUFFIXES[0]))) {
                str_array_push(&limit_files, path);
            }
        }

        free_string_array(&files, file_num);
    }

    free_string_array(&power_supply_dirs, power_supply_num);

    nodes->max_count = str_array_take(&max_files, &nodes->max_files);
    nodes->limit_count = str_array_take(&limit_files, &nodes->limit_files);

    if (nodes->max_count == 0 && nodes->limit_count == 0) {
        *current_change = 0;
        printf_with_time("未找到充电电流控制文件和电流限制文件，电流控制、温控限流和旁路兼容模式不可用；硬件旁路仍会自动探测");
    } else if (nodes->max_count == 0) {
        printf_with_time("未找到电流控制文件，但找到电流限制文件，部分电流控制功能可用");
    }

    if (*current_change) {
        *temp_sensor_num = find_temp_sensor(temp_sensor);

        if (*temp_sensor_num == 0) {
            printf_with_time("找不到支持的温度传感器，温度控制功能失效");
        }
    }

    if (!*step_charge && !*charge_stop_supported && !*current_change) {
        printf_with_time("常规充电控制节点均不存在，硬件旁路仍会在请求时探测");
        printf_with_time("温控移除功能将继续执行");
    }

    for (int i = 0; i < nodes->max_count; i++) {
        printf_with_time("找到电流控制文件：%s", nodes->max_files[i]);
    }

    for (int i = 0; i < nodes->limit_count; i++) {
        printf_with_time("找到电流限制文件：%s", nodes->limit_files[i]);
    }
}

typedef struct {
    int lv1_mc;
    int lv2_mc;
    int max_mc;
    char lv1_current[32];
    char lv2_current[32];
} TempCtrlSettings;

static void read_temp_ctrl_settings(TempCtrlSettings *settings)
{
    settings->lv1_mc = read_one_option("TEMP_LEVEL1") * 1000;
    settings->lv2_mc = read_one_option("TEMP_LEVEL2") * 1000;
    settings->max_mc = read_one_option("TEMP_MAX") * 1000;

    snprintf(settings->lv1_current, sizeof(settings->lv1_current), "%d",
             read_one_option("TEMP_LEVEL1_CURRENT"));
    snprintf(settings->lv2_current, sizeof(settings->lv2_current), "%d",
             read_one_option("TEMP_LEVEL2_CURRENT"));
}

static void read_battery_status(uchar available, char *out, size_t out_size)
{
    out[0] = '\0';

    if (available) read_file(BATTERY_STATUS_PATH, out, out_size);
}

static void read_battery_capacity(uchar available, char *out, size_t out_size)
{
    if (available) {
        read_file(BATTERY_CAPACITY_PATH, out, out_size);
    } else {
        snprintf(out, out_size, "0");
    }
}

/* 传感器温度优先让位于模拟温度；返回 0 表示本轮无法取得温度。 */
static int refresh_temp_mc(const char *temp_sensor, int *temp_mc)
{
    int simulated_temp_mc = current_simulated_temp_mc();

    if (simulated_temp_mc > 0) {
        *temp_mc = simulated_temp_mc;
        return 1;
    }

    return read_temp_mc(temp_sensor, temp_mc);
}

static int get_charging_state(const char *status)
{
    if (!status) return 0;

    return strcmp(status, "Charging") == 0 ||
           strcmp(status, "Full") == 0 ||
           strcmp(status, "Not charging") == 0;
}

static int get_power_connection_state(int battery_status_available,
                                      const char *status)
{
    int status_connected = battery_status_available && get_charging_state(status);
    int external_power = read_external_power_state();

    if (external_power == 1) return 1;
    if (external_power == 0) return 0;

    /* 外部供电节点不可读时才回退到电池状态，避免将拔线后的 Not charging 误判为仍连接。 */
    return battery_status_available ? status_connected : 1;
}

int main(void)
{
    char *temp_sensor = NULL;
    ChargeCurrentNodes charge_current_nodes;
    memset(&charge_current_nodes, 0, sizeof(charge_current_nodes));

    char charge[32] = {0};
    char power[16] = {0};
    char current_max_char[32] = {0};
    char last_appname[APP_PACKAGE_NAME_MAX_SIZE] = {0};

    uchar step_charge = 1;
    uchar power_control = 1;
    uchar charge_stop_supported = 1;
    uchar current_change = 1;
    uchar battery_status = 1;
    uchar battery_capacity = 1;
    uchar logged_limit_fallback = 0;
    uchar logged_temp_limit_fallback = 0;

    int temp_sensor_num = 0;

    int is_first_time = 1;
    int app_bypass_requested = 0;
    int screen_is_off = 0;
    int last_charge_status = 0;
    int last_meizu_device = -1;
    PowerControlState power_control_state = POWER_CONTROL_STATE_INITIALIZER;
    BypassState bypass_state = BYPASS_STATE_INITIALIZER;
    MeizuWiredLevelState meizu_wired_level_state = MEIZU_WIRED_LEVEL_STATE_INITIALIZER;

    unsigned long last_option_generation = (unsigned long)-1;

    TempSimState temp_sim_state;
    memset(&temp_sim_state, 0, sizeof(temp_sim_state));
    temp_sim_state.last_value = -1;
    temp_sim_state.last_simulating = -1;

    MountModeState thermal_mount_state;
    memset(&thermal_mount_state, 0, sizeof(thermal_mount_state));
    thermal_mount_state.last_mode = -1;

    fflush(stdout);

    signal(SIGTERM, handle_exit_signal);
    signal(SIGINT, handle_exit_signal);

    ensure_dir(STATE_DIR);

    pthread_t option_thread;
    pthread_create(&option_thread, NULL, read_option_file_thread, NULL);
    pthread_detach(option_thread);

    sleep(1);

    check_required_files(&battery_status,
                         &battery_capacity,
                         &power_control,
                         &charge_stop_supported,
                         &step_charge,
                         &current_change,
                         &temp_sensor_num,
                         &temp_sensor,
                         &charge_current_nodes);

    int android_version = check_android_version();

    printf_with_time("文件检测完毕，程序开始运行");

    set_value("/sys/kernel/fast_charge/force_fast_charge", "1");
    set_value(BATTERY_SUPPLY_DIR "/system_temp_level", "1");
    set_value(POWER_SUPPLY_DIR "/usb/boost_current", "1");
    set_value(BATTERY_SUPPLY_DIR "/safety_timer_enabled", "0");
    set_value("/sys/kernel/fast_charge/failsafe", "1");
    set_value(BATTERY_SUPPLY_DIR "/allow_hvdcp3", "1");
    set_value(POWER_SUPPLY_DIR "/usb/pd_allowed", "1");
    set_value(BATTERY_SUPPLY_DIR "/input_current_limited", "0");
    set_value(BATTERY_SUPPLY_DIR "/input_current_settled", "1");
    set_value("/sys/class/qcom-battery/restrict_chg", "0");

    set_array_value(charge_current_nodes.limit_files,
                    charge_current_nodes.limit_count, "-1");
    charge_ctl("1");

    while (program_running) {
        int cycle_time = read_one_option("CYCLE_TIME");
        int is_charging = 0;
        int power_stop_requested = 0;
        int power_bypass_requested = 0;

        snprintf(current_max_char, sizeof(current_max_char), "%d",
                 read_one_option("CURRENT_MAX"));

        read_battery_capacity(battery_capacity, power, sizeof(power));
        read_battery_status(battery_status, charge, sizeof(charge));
        is_charging = get_power_connection_state(battery_status, charge);

        sync_thermal_mount_mode(is_charging, &thermal_mount_state);
        handle_option_generation_change(&last_option_generation, &temp_sim_state, is_charging);
        handle_meizu_generation_change(&last_meizu_device, &thermal_mount_state, is_charging);
        apply_battery_temp_simulation(&temp_sim_state, is_charging);
        sync_meizu_wired_level(is_charging, &meizu_wired_level_state);

        apply_step_charge_policy(step_charge, power);
        power_ctl(&power_control_state,
                  power_control && battery_capacity,
                  charge_stop_supported,
                  &power_stop_requested,
                  &power_bypass_requested);

        if (is_charging) {
            bypass_charge_ctl(android_version,
                              last_appname,
                              &app_bypass_requested,
                              &screen_is_off);
        } else {
            if (app_bypass_requested)
                printf_with_time("外部电源已断开，退出应用旁路供电请求");
            app_bypass_requested = 0;
            screen_is_off = 0;
            last_appname[0] = '\0';
            stop_foreground_thread();
        }

        sync_bypass_control(&bypass_state,
                            &power_control_state,
                            is_charging ? power_stop_requested : 0,
                            is_charging ? (power_bypass_requested || app_bypass_requested) : 0,
                            &charge_current_nodes,
                            current_max_char);

        if (!is_charging) {
            if (is_first_time) {
                printf_with_time("充电器未连接");
                is_first_time = 0;
                last_charge_status = 0;
            } else if (last_charge_status) {
                printf_with_time("充电器断开连接");
                last_charge_status = 0;
            }

            sleep(cycle_time);
            continue;
        }

        if (is_first_time || !last_charge_status) {
            printf_with_time("充电器已连接");
            last_charge_status = 1;
            is_first_time = 0;
        }

        if (is_bypass_active(&bypass_state) || power_control_state.stop_applied) {
            sleep(cycle_time);
            continue;
        }

        if (read_bool_option("TEMP_CTRL") &&
            temp_sensor_num == 1 &&
            current_change &&
            temp_sensor != NULL) {
            int temp_mc = 0;

            if (!refresh_temp_mc(temp_sensor, &temp_mc)) {
                sleep(cycle_time);
                continue;
            }

            TempCtrlSettings temp_ctrl;
            read_temp_ctrl_settings(&temp_ctrl);

            while (program_running &&
                   !is_bypass_active(&bypass_state) &&
                   !power_control_state.stop_applied &&
                   temp_mc >= temp_ctrl.lv1_mc) {
                cycle_time = read_one_option("CYCLE_TIME");
                snprintf(current_max_char, sizeof(current_max_char), "%d",
                         read_one_option("CURRENT_MAX"));
                read_temp_ctrl_settings(&temp_ctrl);

                handle_option_generation_change(&last_option_generation, &temp_sim_state, 1);
                handle_meizu_generation_change(&last_meizu_device, &thermal_mount_state, 1);

                if (charge_current_nodes.max_count > 0)
                    set_array_value(charge_current_nodes.limit_files,
                                    charge_current_nodes.limit_count, "-1");

                if (!refresh_temp_mc(temp_sensor, &temp_mc)) break;

                read_battery_status(battery_status, charge, sizeof(charge));

                if (!get_power_connection_state(battery_status, charge)) {
                    sync_meizu_wired_level(0, &meizu_wired_level_state);
                    printf_with_time("充电器断开连接，恢复充电电流为 %s μA", current_max_char);
                    app_bypass_requested = 0;
                    stop_foreground_thread();
                    sync_bypass_control(&bypass_state,
                                        &power_control_state,
                                        0, 0,
                                        &charge_current_nodes,
                                        current_max_char);
                    last_charge_status = 0;
                    break;
                }

                if (temp_mc < temp_ctrl.lv1_mc) break;

                if (!read_bool_option("TEMP_CTRL")) {
                    printf_with_time("温控关闭，恢复充电电流为 %s μA", current_max_char);
                    break;
                }

                sync_meizu_wired_level(1, &meizu_wired_level_state);

                if (temp_mc >= temp_ctrl.max_mc) {
                    printf_with_time("温度 >= %d℃（第三档），停止充电", read_one_option("TEMP_MAX"));
                    charge_ctl("0");
                    while (program_running) {
                        sleep(cycle_time);
                        if (!refresh_temp_mc(temp_sensor, &temp_mc)) break;
                        temp_ctrl.max_mc = read_one_option("TEMP_MAX") * 1000;
                        if (temp_mc < temp_ctrl.max_mc) {
                            printf_with_time("温度降至 %d℃ 以下，恢复充电", read_one_option("TEMP_MAX"));
                            charge_ctl("1");
                            break;
                        }
                    }
                } else {
                    int level2 = temp_mc >= temp_ctrl.lv2_mc;
                    const char *apply_cur = level2 ? temp_ctrl.lv2_current : temp_ctrl.lv1_current;

                    printf_with_time("温度 >= %d℃（%s），限流至 %s μA",
                                     read_one_option(level2 ? "TEMP_LEVEL2" : "TEMP_LEVEL1"),
                                     level2 ? "第二档" : "第一档",
                                     apply_cur);

                    read_battery_capacity(battery_capacity, power, sizeof(power));

                    apply_step_charge_policy(step_charge, power);

                    if (!apply_charge_current(&charge_current_nodes, apply_cur) &&
                        !logged_temp_limit_fallback) {
                        printf_with_time("未找到电流控制文件，使用电流限制文件限制充电电流为 %s μA", apply_cur);
                        logged_temp_limit_fallback = 1;
                    }
                }

                power_ctl(&power_control_state,
                          power_control && battery_capacity,
                          charge_stop_supported,
                          &power_stop_requested,
                          &power_bypass_requested);
                bypass_charge_ctl(android_version,
                                  last_appname,
                                  &app_bypass_requested,
                                  &screen_is_off);
                sync_bypass_control(&bypass_state,
                                    &power_control_state,
                                    power_stop_requested,
                                    power_bypass_requested || app_bypass_requested,
                                    &charge_current_nodes,
                                    current_max_char);

                if (is_bypass_active(&bypass_state) || power_control_state.stop_applied)
                    break;

                sleep(cycle_time);
            }
        }

        if (current_change &&
            !is_bypass_active(&bypass_state) &&
            !power_control_state.stop_applied) {
            if (!restore_charge_current(&charge_current_nodes, current_max_char) &&
                !logged_limit_fallback) {
                printf_with_time("未找到电流控制文件，使用电流限制文件恢复充电");
                logged_limit_fallback = 1;
            }
        }

        sleep(cycle_time);
    }

    printf_with_time("收到退出信号，开始清理运行状态");
    snprintf(current_max_char, sizeof(current_max_char), "%d", read_one_option("CURRENT_MAX"));
    restore_charge_control(&bypass_state,
                           &power_control_state,
                           &charge_current_nodes,
                           current_max_char);
    stop_foreground_thread();
    cleanup_battery_temp_simulation(&temp_sim_state);
    restore_meizu_wired_level(&meizu_wired_level_state);
    umount_thermal_files();

    return 0;
}
