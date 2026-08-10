#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "foreground_app.h"
#include "main.h"
#include "printf_with_time.h"
#include "read_option_file.h"
#include "some_ctrl.h"
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
    int thermal_num = list_dir("/sys/class/thermal", &thermal_dirs);

    int best_index = 9999;
    char best_path[PATH_MAX] = {0};

    for (int i = 0; i < thermal_num; i++) {
        if (!thermal_dirs[i]) continue;
        if (!strstr(thermal_dirs[i], "thermal_zone")) continue;

        char type_path[PATH_MAX] = {0};
        char temp_path[PATH_MAX] = {0};
        char type[64] = {0};
        char temp[32] = {0};

        snprintf(type_path, sizeof(type_path), "%s/type", thermal_dirs[i]);
        snprintf(temp_path, sizeof(temp_path), "%s/temp", thermal_dirs[i]);

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
        *temp_sensor = calloc(1, strlen(best_path) + 1);
        if (!*temp_sensor) return 0;

        strcpy(*temp_sensor, best_path);

        printf_with_time("将使用 %s 温度传感器作为手机温度获取源", temp_sensors[best_index]);
        printf_with_time("温度传感器路径：%s", *temp_sensor);

        return 1;
    }

    return 0;
}

/* 节点列表追加：内存不足时不静默丢弃，而是记录并告知调用方停止扫描。 */
static int append_node_path(char ***list, int *count, int *cap, const char *path)
{
    if (*count >= *cap) {
        int new_cap = *cap * 2;
        char **tmp = realloc(*list, sizeof(char *) * new_cap);

        if (!tmp) {
            printf_with_time("内存不足，充电节点列表停在 %d 项，部分节点不会被控制", *count);
            return 0;
        }

        *list = tmp;
        *cap = new_cap;
    }

    char *copy = strdup(path);
    if (!copy) {
        printf_with_time("内存不足，无法记录充电节点：%s", path);
        return 0;
    }

    (*list)[(*count)++] = copy;
    return 1;
}

static void check_required_files(uchar *battery_status,
                                 uchar *battery_capacity,
                                 uchar *power_control,
                                 uchar *charge_stop_supported,
                                 uchar *step_charge,
                                 uchar *current_change,
                                 int *temp_sensor_num,
                                 char **temp_sensor,
                                 char ***current_max_file,
                                 int *current_max_file_num,
                                 char ***current_limit_file,
                                 int *current_limit_file_num)
{
    if (!file_exists("/sys/class/power_supply/battery/status")) {
        *battery_status = 0;
    }

    if (!file_exists("/sys/class/power_supply/battery/capacity")) {
        *battery_capacity = 0;
    }

    if (!*battery_capacity) {
        *power_control = 0;
        printf_with_time("找不到 battery/capacity，按电量阈值控制功能失效");
    }

    if (!*battery_status) {
        printf_with_time("找不到 battery/status，将尝试通过外部供电节点判断充电器连接状态");
    }

    if (!file_exists("/sys/class/power_supply/battery/charging_enabled") &&
        !file_exists("/sys/class/power_supply/battery/battery_charging_enabled") &&
        !file_exists("/sys/class/power_supply/battery/input_suspend") &&
        !file_exists("/sys/class/qcom-battery/restricted_charging") &&
        !file_exists("/sys/class/qcom-battery/restrict_chg")) {
        *charge_stop_supported = 0;
        printf_with_time("找不到暂停充电控制文件，电量控制的停止充电模式不可用，旁路供电模式仍会尝试运行");
    }

    if (!file_exists("/sys/class/power_supply/battery/step_charging_enabled")) {
        *step_charge = 0;
        printf_with_time("找不到 step_charging_enabled，阶梯式充电控制失效");
    } else if (!*battery_capacity) {
        *step_charge = 2;
        printf_with_time("找不到 capacity，阶梯式充电无法根据电量控制");
    }

    char **power_supply_dirs = NULL;
    int power_supply_num = list_dir("/sys/class/power_supply", &power_supply_dirs);

    int max_cap = 16;
    int limit_cap = 16;

    *current_max_file = calloc(max_cap, sizeof(char *));
    *current_limit_file = calloc(limit_cap, sizeof(char *));

    if (!*current_max_file || !*current_limit_file) {
        printf_with_time("内存不足，无法建立充电节点列表，电流控制与温控限流不可用");
        free(*current_max_file);
        free(*current_limit_file);
        *current_max_file = NULL;
        *current_limit_file = NULL;
        *current_max_file_num = 0;
        *current_limit_file_num = 0;
        *current_change = 0;
        free_string_array(&power_supply_dirs, power_supply_num);
        return;
    }

    for (int i = 0; i < power_supply_num; i++) {
        char **files = NULL;
        int file_num = list_dir(power_supply_dirs[i], &files);

        for (int j = 0; j < file_num; j++) {
            char *path = files[j];
            if (!path) continue;

            if (ends_with(path, "/constant_charge_current_max") ||
                ends_with(path, "/constant_charge_current") ||
                ends_with(path, "/fast_charge_current") ||
                ends_with(path, "/thermal_input_current")) {
                if (!append_node_path(current_max_file, current_max_file_num, &max_cap, path))
                    break;
            } else if (ends_with(path, "/thermal_input_current_limit") ||
                       ends_with(path, "/input_current_limit") ||
                       ends_with(path, "/input_current_max")) {
                if (!append_node_path(current_limit_file, current_limit_file_num, &limit_cap, path))
                    break;
            }
        }

        free_string_array(&files, file_num);
    }

    free_string_array(&power_supply_dirs, power_supply_num);

    if (*current_max_file_num == 0 && *current_limit_file_num == 0) {
        *current_change = 0;
        printf_with_time("未找到充电电流控制文件和电流限制文件，电流控制、温控限流和旁路兼容模式不可用；硬件旁路仍会自动探测");
    } else if (*current_max_file_num == 0) {
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

    for (int i = 0; i < *current_max_file_num; i++) {
        printf_with_time("找到电流控制文件：%s", (*current_max_file)[i]);
    }

    for (int i = 0; i < *current_limit_file_num; i++) {
        printf_with_time("找到电流限制文件：%s", (*current_limit_file)[i]);
    }
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
    char **current_limit_file = NULL;
    char **current_max_file = NULL;

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
    uchar logged_status_read_failure = 0;

    int temp_sensor_num = 0;
    int current_limit_file_num = 0;
    int current_max_file_num = 0;

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
    int option_thread_ret = pthread_create(&option_thread, NULL, read_option_file_thread, NULL);

    if (option_thread_ret == 0) {
        pthread_detach(option_thread);
    } else {
        printf_with_time("配置线程启动失败（%s），将使用内置默认配置且不再热重载配置文件",
                         strerror(option_thread_ret));
    }

    sleep(1);

    check_required_files(&battery_status,
                         &battery_capacity,
                         &power_control,
                         &charge_stop_supported,
                         &step_charge,
                         &current_change,
                         &temp_sensor_num,
                         &temp_sensor,
                         &current_max_file,
                         &current_max_file_num,
                         &current_limit_file,
                         &current_limit_file_num);

    int android_version = check_android_version();

    printf_with_time("文件检测完毕，程序开始运行");

    set_value("/sys/kernel/fast_charge/force_fast_charge", "1");
    set_value("/sys/class/power_supply/battery/system_temp_level", "1");
    set_value("/sys/class/power_supply/usb/boost_current", "1");
    set_value("/sys/class/power_supply/battery/safety_timer_enabled", "0");
    set_value("/sys/kernel/fast_charge/failsafe", "1");
    set_value("/sys/class/power_supply/battery/allow_hvdcp3", "1");
    set_value("/sys/class/power_supply/usb/pd_allowed", "1");
    set_value("/sys/class/power_supply/battery/input_current_limited", "0");
    set_value("/sys/class/power_supply/battery/input_current_settled", "1");
    set_value("/sys/class/qcom-battery/restrict_chg", "0");

    set_array_value(current_limit_file, current_limit_file_num, "-1");
    charge_ctl("1");

    while (program_running) {
        int cycle_time = read_one_option("CYCLE_TIME");
        int is_charging = 0;
        int power_stop_requested = 0;
        int power_bypass_requested = 0;

        snprintf(current_max_char, sizeof(current_max_char), "%d",
                 read_one_option("CURRENT_MAX"));

        int power_valid = 0;

        if (battery_capacity) {
            power_valid = read_file("/sys/class/power_supply/battery/capacity",
                                    power, sizeof(power));
        } else {
            strcpy(power, "0");
        }

        charge[0] = '\0';
        if (battery_status) {
            if (!read_file("/sys/class/power_supply/battery/status", charge, sizeof(charge))) {
                if (!logged_status_read_failure) {
                    printf_with_time("无法读取 battery/status，改用外部供电节点判断充电器连接状态");
                    logged_status_read_failure = 1;
                }
            } else {
                logged_status_read_failure = 0;
            }
        }
        is_charging = get_power_connection_state(battery_status, charge);

        sync_thermal_mount_mode(is_charging, &thermal_mount_state);
        handle_option_generation_change(&last_option_generation, &temp_sim_state, is_charging);
        handle_meizu_generation_change(&last_meizu_device, &thermal_mount_state, is_charging);
        apply_battery_temp_simulation(&temp_sim_state, is_charging);
        sync_meizu_wired_level(is_charging, &meizu_wired_level_state);

        apply_step_charge_policy(step_charge, power, power_valid || !battery_capacity);
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
                            current_max_file,
                            current_max_file_num,
                            current_limit_file,
                            current_limit_file_num,
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

        if (read_one_option("TEMP_CTRL") == 1 &&
            temp_sensor_num == 1 &&
            current_change &&
            temp_sensor != NULL) {
            int temp_mc = 0;
            int simulated_temp_mc = current_simulated_temp_mc();

            if (simulated_temp_mc > 0) {
                temp_mc = simulated_temp_mc;
            } else if (!read_temp_mc(temp_sensor, &temp_mc)) {
                sleep(cycle_time);
                continue;
            }

            int lv1_mc = read_one_option("TEMP_LEVEL1") * 1000;
            int lv2_mc = read_one_option("TEMP_LEVEL2") * 1000;
            int max_mc = read_one_option("TEMP_MAX") * 1000;
            char lv1_cur[32], lv2_cur[32];
            snprintf(lv1_cur, sizeof(lv1_cur), "%d", read_one_option("TEMP_LEVEL1_CURRENT"));
            snprintf(lv2_cur, sizeof(lv2_cur), "%d", read_one_option("TEMP_LEVEL2_CURRENT"));

            while (program_running &&
                   !is_bypass_active(&bypass_state) &&
                   !power_control_state.stop_applied &&
                   temp_mc >= lv1_mc) {
                cycle_time = read_one_option("CYCLE_TIME");
                snprintf(current_max_char, sizeof(current_max_char), "%d",
                         read_one_option("CURRENT_MAX"));
                lv1_mc = read_one_option("TEMP_LEVEL1") * 1000;
                lv2_mc = read_one_option("TEMP_LEVEL2") * 1000;
                max_mc = read_one_option("TEMP_MAX") * 1000;
                snprintf(lv1_cur, sizeof(lv1_cur), "%d", read_one_option("TEMP_LEVEL1_CURRENT"));
                snprintf(lv2_cur, sizeof(lv2_cur), "%d", read_one_option("TEMP_LEVEL2_CURRENT"));

                handle_option_generation_change(&last_option_generation, &temp_sim_state, 1);
                handle_meizu_generation_change(&last_meizu_device, &thermal_mount_state, 1);

                if (current_max_file_num > 0)
                    set_array_value(current_limit_file, current_limit_file_num, "-1");

                simulated_temp_mc = current_simulated_temp_mc();
                if (simulated_temp_mc > 0) {
                    temp_mc = simulated_temp_mc;
                } else if (!read_temp_mc(temp_sensor, &temp_mc)) {
                    break;
                }

                charge[0] = '\0';
                if (battery_status) {
                    read_file("/sys/class/power_supply/battery/status", charge, sizeof(charge));
                }

                if (!get_power_connection_state(battery_status, charge)) {
                    sync_meizu_wired_level(0, &meizu_wired_level_state);
                    printf_with_time("充电器断开连接，恢复充电电流为 %s μA", current_max_char);
                    app_bypass_requested = 0;
                    stop_foreground_thread();
                    sync_bypass_control(&bypass_state,
                                        &power_control_state,
                                        0, 0,
                                        current_max_file,
                                        current_max_file_num,
                                        current_limit_file,
                                        current_limit_file_num,
                                        current_max_char);
                    last_charge_status = 0;
                    break;
                }

                if (temp_mc < lv1_mc) break;

                if (read_one_option("TEMP_CTRL") != 1) {
                    printf_with_time("温控关闭，恢复充电电流为 %s μA", current_max_char);
                    break;
                }

                sync_meizu_wired_level(1, &meizu_wired_level_state);

                if (temp_mc >= max_mc) {
                    printf_with_time("温度 >= %d℃（第三档），停止充电", read_one_option("TEMP_MAX"));
                    int temp_stop_applied = charge_ctl("0");
                    while (program_running) {
                        sleep(cycle_time);
                        simulated_temp_mc = current_simulated_temp_mc();
                        if (simulated_temp_mc > 0) temp_mc = simulated_temp_mc;
                        else if (!read_temp_mc(temp_sensor, &temp_mc)) break;
                        max_mc = read_one_option("TEMP_MAX") * 1000;

                        /* 上一次停充写入失败时继续重试，不假设已经停充。 */
                        if (!temp_stop_applied && temp_mc >= max_mc)
                            temp_stop_applied = charge_ctl("0");

                        if (temp_mc < max_mc) {
                            printf_with_time("温度降至 %d℃ 以下，恢复充电", read_one_option("TEMP_MAX"));
                            charge_ctl("1");
                            break;
                        }
                    }
                } else {
                    const char *apply_cur = (temp_mc >= lv2_mc) ? lv2_cur : lv1_cur;
                    if (temp_mc >= lv2_mc)
                        printf_with_time("温度 >= %d℃（第二档），限流至 %s μA", read_one_option("TEMP_LEVEL2"), lv2_cur);
                    else
                        printf_with_time("温度 >= %d℃（第一档），限流至 %s μA", read_one_option("TEMP_LEVEL1"), lv1_cur);

                    int temp_power_valid = !battery_capacity;
                    if (battery_capacity)
                        temp_power_valid = read_file("/sys/class/power_supply/battery/capacity",
                                                     power, sizeof(power));

                    apply_step_charge_policy(step_charge, power, temp_power_valid);

                    if (current_max_file_num > 0)
                        set_array_value(current_max_file, current_max_file_num, apply_cur);
                    else {
                        if (!logged_temp_limit_fallback) {
                            printf_with_time("未找到电流控制文件，使用电流限制文件限制充电电流为 %s μA", apply_cur);
                            logged_temp_limit_fallback = 1;
                        }
                        set_array_value(current_limit_file, current_limit_file_num, apply_cur);
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
                                    current_max_file,
                                    current_max_file_num,
                                    current_limit_file,
                                    current_limit_file_num,
                                    current_max_char);

                if (is_bypass_active(&bypass_state) || power_control_state.stop_applied)
                    break;

                sleep(cycle_time);
            }
        }

        if (current_change &&
            !is_bypass_active(&bypass_state) &&
            !power_control_state.stop_applied) {
            if (current_max_file_num > 0)
                set_array_value(current_max_file, current_max_file_num, current_max_char);
            else {
                if (!logged_limit_fallback) {
                    printf_with_time("未找到电流控制文件，使用电流限制文件恢复充电");
                    logged_limit_fallback = 1;
                }
                set_array_value(current_limit_file, current_limit_file_num, "-1");
            }
        }

        sleep(cycle_time);
    }

    printf_with_time("收到退出信号，开始清理运行状态");
    snprintf(current_max_char, sizeof(current_max_char), "%d", read_one_option("CURRENT_MAX"));
    restore_charge_control(&bypass_state,
                           &power_control_state,
                           current_max_file,
                           current_max_file_num,
                           current_limit_file,
                           current_limit_file_num,
                           current_max_char);
    stop_foreground_thread();
    cleanup_battery_temp_simulation(&temp_sim_state);
    restore_meizu_wired_level(&meizu_wired_level_state);
    umount_thermal_files();

    return 0;
}
