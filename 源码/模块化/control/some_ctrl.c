#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "foreground_app.h"
#include "printf_with_time.h"
#include "read_option_file.h"
#include "some_ctrl.h"
#include "thermal_mount.h"
#include "value_set.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
void handle_meizu_generation_change(int *last_meizu_thermal_key,
                                            MountModeState *thermal_mount_state,
                                            int is_charging)
{
    if (!last_meizu_thermal_key || !thermal_mount_state) return;

    int meizu_device = read_one_option("MEIZU_DEVICE") == 1 ? 1 : 0;
    int scheme = clamp_meizu_thermal_scheme(read_one_option("MEIZU_THERMAL_SCHEME"));
    int cur = meizu_device ? scheme : 0;

    if (*last_meizu_thermal_key == -1) {
        *last_meizu_thermal_key = cur;
        return;
    }

    if (cur == *last_meizu_thermal_key) return;

    int old = *last_meizu_thermal_key;
    *last_meizu_thermal_key = cur;
    printf_with_time("Meizu thermal selection changed, old_key=%d, new_key=%d, MEIZU_DEVICE=%d, MEIZU_THERMAL_SCHEME=%d(%s)",
                     old, cur, meizu_device, scheme, meizu_thermal_scheme_name(scheme));
    printf_with_time("刷新温控挂载，当前充电=%d，THERMAL_MOUNT_MODE=%d",
                     is_charging, read_one_option("THERMAL_MOUNT_MODE"));

    if (thermal_mount_state->mounted) {
        umount_thermal_files();
        thermal_mount_state->mounted = 0;
    }

    sync_thermal_mount_mode(is_charging, thermal_mount_state);
}

void step_charge_ctl(const char *value)
{
    set_value("/sys/class/power_supply/battery/step_charging_enabled", value);
    set_value("/sys/class/power_supply/battery/sw_jeita_enabled", value);
}

void charge_ctl(const char *value)
{
    set_value("/sys/class/power_supply/battery/charging_enabled", value);
    set_value("/sys/class/power_supply/battery/battery_charging_enabled", value);

    if (atoi(value)) {
        set_value("/sys/class/power_supply/battery/input_suspend", "0");
        set_value("/sys/class/qcom-battery/restricted_charging", "0");
        set_value("/sys/class/qcom-battery/restrict_chg", "0");
    } else {
        set_value("/sys/class/power_supply/battery/input_suspend", "1");
        set_value("/sys/class/qcom-battery/restricted_charging", "1");
        set_value("/sys/class/qcom-battery/restrict_chg", "1");
    }
}

void restore_meizu_wired_level(MeizuWiredLevelState *state)
{
    if (!state) return;

    int last_write_result = state->last_write_result;
    if (last_write_result >= 1) {
        printf_with_time("准备恢复魅族充电档位节点写权限，当前锁定档位=%d", last_write_result);
    }

    if (last_write_result >= 1 || last_write_result == MEIZU_LEVEL_WRITE_FAILED) {
        int ret = restore_meizu_wired_level_permission();
        if (ret != 0) {
            printf_with_time("恢复魅族充电档位节点写权限失败，路径=%s，返回码=%d",
                             MEIZU_WIRED_LEVEL_PATHS_TEXT, ret);
        } else {
            printf_with_time("魅族充电档位节点已恢复写权限，路径=%s", MEIZU_WIRED_LEVEL_PATHS_TEXT);
        }
    }

    state->last_attempted_level = MEIZU_LEVEL_INACTIVE;
    state->last_write_result = MEIZU_LEVEL_INACTIVE;
}

void sync_meizu_wired_level(int is_charging, MeizuWiredLevelState *state)
{
    if (!state) return;

    int meizu_device = read_one_option("MEIZU_DEVICE") == 1;
    MeizuWiredLevelMode next_mode = MEIZU_WIRED_LEVEL_MODE_ACTIVE;
    if (!meizu_device) {
        next_mode = MEIZU_WIRED_LEVEL_MODE_DISABLED;
    } else if (!is_charging) {
        next_mode = MEIZU_WIRED_LEVEL_MODE_NOT_CHARGING;
    }

    if (next_mode != MEIZU_WIRED_LEVEL_MODE_ACTIVE) {
        if (state->mode == next_mode) return;

        if (next_mode == MEIZU_WIRED_LEVEL_MODE_DISABLED) {
            printf_with_time("魅族适配未开启，跳过档位写入");
        } else {
            printf_with_time("当前未在充电，恢复魅族档位节点写权限");
        }
        restore_meizu_wired_level(state);
        state->mode = next_mode;
        return;
    }

    if (state->mode != MEIZU_WIRED_LEVEL_MODE_ACTIVE) {
        state->last_attempted_level = MEIZU_LEVEL_INACTIVE;
        state->last_write_result = MEIZU_LEVEL_INACTIVE;
        state->mode = MEIZU_WIRED_LEVEL_MODE_ACTIVE;
    }

    int raw_level = read_one_option("MEIZU_CHARGE_LEVEL");
    int level = clamp_meizu_charge_level(raw_level);
    if (state->last_attempted_level == level) return;

    state->last_attempted_level = level;
    printf_with_time("准备写入魅族充电档位，原始值=%d，修正后=%d，候选节点=%s",
                     raw_level, level, MEIZU_WIRED_LEVEL_PATHS_TEXT);
    int found_nodes = 0;
    int success_nodes = 0;
    int ret = write_meizu_wired_level(level, &found_nodes, &success_nodes);

    if (ret == MEIZU_LEVEL_NODE_MISSING) {
        printf_with_time("Meizu wired_level nodes missing, paths=%s,%s",
                         MEIZU_WIRED_LEVEL_PATH, MEIZU_WIRED_LEVEL_LEGACY_PATH);
        state->last_write_result = MEIZU_LEVEL_NODE_MISSING;
        return;
    }

    if (ret != 0) {
        printf_with_time("魅族充电档位写入失败，档位=%d，候选节点=%s，返回码=%d，成功节点=%d/%d",
                         level, MEIZU_WIRED_LEVEL_PATHS_TEXT, ret, success_nodes, found_nodes);
        state->last_write_result = MEIZU_LEVEL_WRITE_FAILED;
        return;
    }

    printf_with_time("Meizu wired_level write complete, level=%d, success=%d, found=%d",
                     level, success_nodes, found_nodes);
    printf_with_time("魅族充电档位写入成功，已锁定为 %d，成功节点=%d/%d，候选节点=%s",
                     level, success_nodes, found_nodes, MEIZU_WIRED_LEVEL_PATHS_TEXT);
    state->last_write_result = level;
}

#define EXTERNAL_POWER_NODE_MAX 32
#define BYPASS_RETRY_SECONDS 30
#define BYPASS_RESTORE_RETRIES 3
#define BYPASS_RESTORE_DELAY_US 100000

static char *external_power_nodes[EXTERNAL_POWER_NODE_MAX];
static int external_power_node_count = 0;
static int external_power_nodes_discovered = 0;

static int is_battery_power_supply(const char *dir)
{
    if (!dir) return 1;

    const char *base = strrchr(dir, '/');
    base = base ? base + 1 : dir;

    if (contains_ignore_case(base, "battery") ||
        contains_ignore_case(base, "bms")) {
        return 1;
    }

    char type_path[PATH_MAX] = {0};
    char type[64] = {0};
    snprintf(type_path, sizeof(type_path), "%s/type", dir);

    return read_file(type_path, type, sizeof(type)) &&
           (contains_ignore_case(type, "Battery") ||
            contains_ignore_case(type, "BMS"));
}

static void discover_external_power_nodes(void)
{
    if (external_power_nodes_discovered && external_power_node_count > 0) return;

    external_power_nodes_discovered = 1;

    char **power_supply_dirs = NULL;
    int dir_num = list_dir("/sys/class/power_supply", &power_supply_dirs);

    for (int i = 0; i < dir_num && external_power_node_count < EXTERNAL_POWER_NODE_MAX; i++) {
        const char *dir = power_supply_dirs[i];
        if (!dir || is_battery_power_supply(dir)) continue;

        const char *names[] = {"present", "online"};
        for (int j = 0; j < 2 && external_power_node_count < EXTERNAL_POWER_NODE_MAX; j++) {
            char path[PATH_MAX] = {0};
            snprintf(path, sizeof(path), "%s/%s", dir, names[j]);
            if (!file_exists(path)) continue;

            external_power_nodes[external_power_node_count] = strdup(path);
            if (external_power_nodes[external_power_node_count]) {
                external_power_node_count++;
            }
        }
    }

    free_string_array(&power_supply_dirs, dir_num);
}

int read_external_power_state(void)
{
    discover_external_power_nodes();

    int present_readable = 0;
    int present_connected = 0;
    int online_readable = 0;
    int online_connected = 0;

    for (int i = 0; i < external_power_node_count; i++) {
        char value[32] = {0};
        if (!read_file(external_power_nodes[i], value, sizeof(value))) continue;

        const char *base = strrchr(external_power_nodes[i], '/');
        base = base ? base + 1 : external_power_nodes[i];

        if (strcmp(base, "present") == 0) {
            present_readable = 1;
            if (atoi(value) > 0) present_connected = 1;
        } else {
            online_readable = 1;
            if (atoi(value) > 0) online_connected = 1;
        }
    }

    if (present_connected || online_connected) return 1;
    if (present_readable || online_readable) return 0;
    return -1;
}

static int text_equals_ignore_case(const char *left, const char *right)
{
    return left && right && strcasecmp(left, right) == 0;
}

static int write_bypass_value(const char *path, const char *value)
{
    char verify[128] = {0};

    if (!path || !value || !file_exists(path)) return 0;

    set_value(path, value);
    return read_file(path, verify, sizeof(verify)) &&
           text_equals_ignore_case(verify, value);
}

static void clear_bypass_state(BypassState *state)
{
    if (!state) return;

    state->mode = BYPASS_MODE_OFF;
    state->node_path[0] = '\0';
    state->restore_value[0] = '\0';
    state->active_value[0] = '\0';
    state->restore_warning_logged = 0;
    state->next_probe_time = 0;
}

static int remember_hardware_bypass(BypassState *state,
                                    const char *path,
                                    const char *restore_value,
                                    const char *active_value)
{
    if (!state || !path || !restore_value || !active_value) return 0;

    state->mode = BYPASS_MODE_HARDWARE;
    snprintf(state->node_path, sizeof(state->node_path), "%s", path);
    snprintf(state->restore_value, sizeof(state->restore_value), "%s", restore_value);
    snprintf(state->active_value, sizeof(state->active_value), "%s", active_value);
    state->restore_warning_logged = 0;
    state->next_probe_time = 0;
    return 1;
}

static int try_enable_hardware_node(BypassState *state,
                                    const char *path,
                                    const char *active_value)
{
    char original[128] = {0};

    if (!state || !path || !active_value) return 0;
    if (!read_file(path, original, sizeof(original)) || original[0] == '\0') return 0;

    if (!text_equals_ignore_case(original, active_value) &&
        !write_bypass_value(path, active_value)) {
        set_value(path, original);
        return 0;
    }

    remember_hardware_bypass(state, path, original, active_value);
    printf_with_time("旁路供电已启用硬件节点：%s（%s）", path, active_value);
    return 1;
}

static int derive_generic_bypass_value(const char *current,
                                       char *active,
                                       size_t active_size)
{
    if (!current || !active || active_size == 0) return 0;

    if (text_equals_ignore_case(current, "0"))
        snprintf(active, active_size, "1");
    else if (text_equals_ignore_case(current, "off"))
        snprintf(active, active_size, "on");
    else if (text_equals_ignore_case(current, "disabled"))
        snprintf(active, active_size, "enabled");
    else if (text_equals_ignore_case(current, "false"))
        snprintf(active, active_size, "true");
    else if (text_equals_ignore_case(current, "no"))
        snprintf(active, active_size, "yes");
    else if (text_equals_ignore_case(current, "1") ||
             text_equals_ignore_case(current, "on") ||
             text_equals_ignore_case(current, "enabled") ||
             text_equals_ignore_case(current, "true") ||
             text_equals_ignore_case(current, "yes"))
        snprintf(active, active_size, "%s", current);
    else
        return 0;

    return 1;
}

static int is_generic_bypass_node(const char *path)
{
    if (!path) return 0;

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    const char *names[] = {
        "bypass_charging",
        "bypass_charge",
        "bypass_chg",
        "charge_bypass",
        "charger_bypass",
        "bypass_charger"
    };

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcmp(base, names[i]) == 0) return 1;
    }

    return 0;
}

static int try_standard_charge_type_bypass(BypassState *state)
{
    char **power_supply_dirs = NULL;
    int dir_num = list_dir("/sys/class/power_supply", &power_supply_dirs);
    int enabled = 0;

    for (int i = 0; i < dir_num && !enabled; i++) {
        char types_path[PATH_MAX] = {0};
        char type_path[PATH_MAX] = {0};
        char charge_types[256] = {0};

        snprintf(types_path, sizeof(types_path), "%s/charge_types", power_supply_dirs[i]);
        snprintf(type_path, sizeof(type_path), "%s/charge_type", power_supply_dirs[i]);

        if (!file_exists(type_path) ||
            !read_file(types_path, charge_types, sizeof(charge_types)) ||
            !contains_ignore_case(charge_types, "Bypass")) {
            continue;
        }

        enabled = try_enable_hardware_node(state, type_path, "Bypass");
    }

    free_string_array(&power_supply_dirs, dir_num);
    return enabled;
}

static int try_known_hardware_node(BypassState *state,
                                           const char *path,
                                           const char *preferred_value)
{
    char original[128] = {0};
    char derived_value[32] = {0};

    if (!state || !path || !preferred_value || !file_exists(path)) return 0;
    if (try_enable_hardware_node(state, path, preferred_value)) return 1;

    if (!read_file(path, original, sizeof(original)) ||
        !derive_generic_bypass_value(original, derived_value, sizeof(derived_value)) ||
        text_equals_ignore_case(derived_value, preferred_value)) {
        return 0;
    }

    return try_enable_hardware_node(state, path, derived_value);
}

static int try_known_hardware_bypass(BypassState *state)
{
    struct {
        const char *path;
        const char *active_value;
    } candidates[] = {
        {"/sys/kernel/nubia_charge/charger_bypass", "on"},
        {"/sys/devices/platform/charger/bypass_charger", "1"},
        {"/sys/class/power_supply/battery/bypass_charging", "1"},
        {"/sys/class/power_supply/battery/bypass_charge", "1"},
        {"/sys/class/power_supply/battery/bypass_chg", "1"},
        {"/sys/class/power_supply/battery/charge_bypass", "1"},
        {"/sys/class/power_supply/battery/charger_bypass", "1"},
        {"/sys/class/power_supply/battery/bypass_charger", "1"}
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (try_known_hardware_node(state,
                                    candidates[i].path,
                                    candidates[i].active_value)) {
            return 1;
        }
    }

    return 0;
}

static int try_discovered_hardware_bypass(BypassState *state)
{
    char **power_supply_dirs = NULL;
    int dir_num = list_dir("/sys/class/power_supply", &power_supply_dirs);
    int enabled = 0;

    for (int i = 0; i < dir_num && !enabled; i++) {
        char **files = NULL;
        int file_num = list_dir(power_supply_dirs[i], &files);

        for (int j = 0; j < file_num && !enabled; j++) {
            char original[128] = {0};
            char active[32] = {0};

            if (!is_generic_bypass_node(files[j]) ||
                !read_file(files[j], original, sizeof(original)) ||
                !derive_generic_bypass_value(original, active, sizeof(active))) {
                continue;
            }

            enabled = try_enable_hardware_node(state, files[j], active);
        }

        free_string_array(&files, file_num);
    }

    free_string_array(&power_supply_dirs, dir_num);
    return enabled;
}

static int enable_hardware_bypass(BypassState *state)
{
    return try_standard_charge_type_bypass(state) ||
           try_known_hardware_bypass(state) ||
           try_discovered_hardware_bypass(state);
}

static int restore_hardware_bypass(BypassState *state)
{
    if (!state || state->mode != BYPASS_MODE_HARDWARE) return 1;

    if (!file_exists(state->node_path)) {
        printf_with_time("硬件旁路节点已消失，清除旧状态并重新探测：%s", state->node_path);
        clear_bypass_state(state);
        return 1;
    }

    if (write_bypass_value(state->node_path, state->restore_value)) {
        printf_with_time("硬件旁路已退出，节点恢复为原值：%s（%s）",
                         state->node_path, state->restore_value);
        clear_bypass_state(state);
        return 1;
    }

    if (!state->restore_warning_logged) {
        printf_with_time("硬件旁路节点恢复失败，将继续重试：%s（目标值 %s）",
                         state->node_path, state->restore_value);
        state->restore_warning_logged = 1;
    }
    return 0;
}

static void restore_normal_current(char **current_max_file,
                                   int current_max_file_num,
                                   char **current_limit_file,
                                   int current_limit_file_num,
                                   const char *normal_current)
{
    if (current_max_file_num > 0 && normal_current) {
        set_array_value(current_max_file, current_max_file_num, normal_current);
    } else {
        set_array_value(current_limit_file, current_limit_file_num, "-1");
    }
}

static int sync_bypass_supply(BypassState *state,
                              int requested,
                              char **current_max_file,
                              int current_max_file_num,
                              char **current_limit_file,
                              int current_limit_file_num,
                              const char *normal_current)
{
    if (!state) return 0;

    if (!requested) {
        if (state->mode == BYPASS_MODE_HARDWARE) {
            restore_hardware_bypass(state);
        } else if (state->mode == BYPASS_MODE_COMPATIBILITY) {
            restore_normal_current(current_max_file, current_max_file_num,
                                   current_limit_file, current_limit_file_num,
                                   normal_current);
            printf_with_time("旁路供电兼容模式已退出，恢复正常充电电流");
            clear_bypass_state(state);
        } else if (state->mode == BYPASS_MODE_UNAVAILABLE) {
            clear_bypass_state(state);
        }

        return is_bypass_active(state);
    }

    if (state->mode == BYPASS_MODE_HARDWARE) {
        char current[128] = {0};
        if (read_file(state->node_path, current, sizeof(current)) &&
            text_equals_ignore_case(current, state->active_value)) {
            return 1;
        }

        if (write_bypass_value(state->node_path, state->active_value)) {
            state->restore_warning_logged = 0;
            return 1;
        }

        printf_with_time("硬件旁路节点失效，尝试恢复原值并切换兼容模式：%s",
                         state->node_path);
        if (!restore_hardware_bypass(state)) return 1;
    }

    if (state->mode == BYPASS_MODE_COMPATIBILITY) {
        if (current_max_file_num > 0)
            set_array_value(current_max_file, current_max_file_num, BYPASS_CHARGE_CURRENT);
        else
            set_array_value(current_limit_file, current_limit_file_num, BYPASS_CHARGE_CURRENT);
        return 1;
    }

    if (state->mode == BYPASS_MODE_UNAVAILABLE) {
        time_t now = time(NULL);
        if (state->next_probe_time > now) return 0;
        clear_bypass_state(state);
    }

    if (enable_hardware_bypass(state)) return 1;

    if (current_max_file_num > 0 || current_limit_file_num > 0) {
        state->mode = BYPASS_MODE_COMPATIBILITY;
        printf_with_time("设备未找到可验证的硬件旁路节点，使用 500mA 兼容模式");

        if (current_max_file_num > 0)
            set_array_value(current_max_file, current_max_file_num, BYPASS_CHARGE_CURRENT);
        else
            set_array_value(current_limit_file, current_limit_file_num, BYPASS_CHARGE_CURRENT);
        return 1;
    }

    state->mode = BYPASS_MODE_UNAVAILABLE;
    state->next_probe_time = time(NULL) + BYPASS_RETRY_SECONDS;
    printf_with_time("旁路供电不可用：未找到硬件旁路节点，也没有可用的电流控制节点；稍后将自动重试");
    return 0;
}

int is_bypass_active(const BypassState *state)
{
    return state &&
           (state->mode == BYPASS_MODE_HARDWARE ||
            state->mode == BYPASS_MODE_COMPATIBILITY);
}

void power_ctl(PowerControlState *state,
               int capacity_available,
               int stop_supported,
               int *stop_requested,
               int *bypass_requested)
{
    if (!state || !stop_requested || !bypass_requested) return;

    *stop_requested = 0;
    *bypass_requested = 0;

    int configured_charge_stop = read_one_option("CHARGE_STOP");
    int configured_charge_start = read_one_option("CHARGE_START");
    int charge_stop = configured_charge_stop;
    int charge_start = configured_charge_start;
    int thresholds_adjusted = 0;

    if (charge_stop < 1) {
        charge_stop = 1;
        thresholds_adjusted = 1;
    } else if (charge_stop > 100) {
        charge_stop = 100;
        thresholds_adjusted = 1;
    }

    if (charge_start < 0) {
        charge_start = 0;
        thresholds_adjusted = 1;
    } else if (charge_start > 100) {
        charge_start = 100;
        thresholds_adjusted = 1;
    }

    if (charge_start >= charge_stop) {
        charge_start = charge_stop - 1;
        thresholds_adjusted = 1;
    }

    int mode = read_one_option("POWER_CTRL_MODE") == 1 ? 1 : 0;
    int previous_mode = state->last_mode;

    if (state->last_charge_stop == -1) state->last_charge_stop = charge_stop;
    if (state->last_mode == -1) previous_mode = mode;

    if (read_one_option("POWER_CTRL") != 1 || !capacity_available) {
        if (state->active) {
            printf_with_time("电量控制关闭，退出当前%s状态",
                             previous_mode == 1 ? "旁路供电" : "停止充电");
        }
        state->active = 0;
        state->stop_unsupported_logged = 0;
        state->threshold_warning_logged = 0;
        state->last_charge_stop = charge_stop;
        state->last_mode = mode;
        return;
    }

    if (thresholds_adjusted) {
        if (!state->threshold_warning_logged) {
            printf_with_time("电量阈值配置无效：CHARGE_STOP=%d，CHARGE_START=%d；本次运行按 CHARGE_STOP=%d、CHARGE_START=%d 处理",
                             configured_charge_stop, configured_charge_start,
                             charge_stop, charge_start);
        }
        state->threshold_warning_logged = 1;
    } else {
        state->threshold_warning_logged = 0;
    }

    char power[16] = {0};
    if (!read_file("/sys/class/power_supply/battery/capacity", power, sizeof(power))) {
        printf_with_time("无法读取电量，保持上次电量控制状态");
    } else {
        int power_int = atoi(power);
        int was_active = state->active;

        if (state->last_charge_stop != charge_stop && state->active &&
            power_int < charge_stop) {
            printf_with_time("当前电量 %d%%，小于新的停止阈值，退出电量控制状态", power_int);
            state->active = 0;
        }

        if (!state->active && power_int >= charge_stop) {
            state->active = 1;
            printf_with_time("当前电量 %d%%，大于等于停止阈值，请求%s",
                             power_int, mode == 1 ? "旁路供电" : "停止充电");
        } else if (state->active && power_int <= charge_start) {
            state->active = 0;
            printf_with_time("当前电量 %d%%，小于等于恢复阈值，恢复正常充电", power_int);
        }

        if (was_active && state->active && previous_mode != mode) {
            printf_with_time("电量控制模式已切换为%s",
                             mode == 1 ? "旁路供电" : "停止充电");
        }
    }

    state->last_charge_stop = charge_stop;
    state->last_mode = mode;

    if (!state->active) {
        state->stop_unsupported_logged = 0;
        return;
    }

    if (mode == 1) {
        state->stop_unsupported_logged = 0;
        *bypass_requested = 1;
    } else if (stop_supported) {
        state->stop_unsupported_logged = 0;
        *stop_requested = 1;
    } else if (!state->stop_unsupported_logged) {
        printf_with_time("无法执行停止充电：未找到可用的暂停充电控制节点");
        state->stop_unsupported_logged = 1;
    }
}

void bypass_charge_ctl(int android_version,
                       char *last_appname,
                       int *app_bypass_requested,
                       int *screen_is_off)
{
    static char **bypass_apps = NULL;
    static int bypass_app_num = 0;
    static time_t bypass_file_last_mtime = 0;

    if (!last_appname || !app_bypass_requested || !screen_is_off) return;

    if (read_one_option("BYPASS_CHARGE") != 1) {
        if (*app_bypass_requested) {
            printf_with_time("应用旁路供电关闭，退出旁路请求");
        }

        *app_bypass_requested = 0;
        *screen_is_off = 0;
        last_appname[0] = '\0';
        stop_foreground_thread();
        return;
    }

    if (android_version <= 0) {
        *app_bypass_requested = 0;
        return;
    }

    start_foreground_thread_if_needed(android_version);

    char fg[APP_PACKAGE_NAME_MAX_SIZE] = {0};
    get_foreground_app(fg, sizeof(fg));

    if (fg[0] == '\0') return;

    if (strcmp(fg, "screen_is_off") == 0) {
        if (!*screen_is_off) {
            if (*app_bypass_requested)
                printf_with_time("手机屏幕关闭，暂时退出应用旁路供电");
            *app_bypass_requested = 0;
            *screen_is_off = 1;
        }
        return;
    }

    if (*screen_is_off) {
        printf_with_time("手机屏幕开启，恢复应用旁路供电判断");
        *screen_is_off = 0;
    }

    if (!load_bypass_app_list(&bypass_apps, &bypass_app_num,
                              &bypass_file_last_mtime)) {
        return;
    }

    int in_list = 0;
    for (int i = 0; i < bypass_app_num; i++) {
        if (bypass_apps[i] && strcmp(fg, bypass_apps[i]) == 0) {
            in_list = 1;
            break;
        }
    }

    if (in_list) {
        if (!*app_bypass_requested) {
            printf_with_time("当前前台应用 %s 位于旁路供电列表中，请求旁路供电", fg);
        } else if (strcmp(last_appname, fg) != 0) {
            printf_with_time("前台应用切换为 %s，保持旁路供电", fg);
        }
        *app_bypass_requested = 1;
    } else {
        if (*app_bypass_requested) {
            printf_with_time("前台应用 %s 不在旁路供电列表中，退出应用旁路供电", fg);
        }
        *app_bypass_requested = 0;
    }

    snprintf(last_appname, APP_PACKAGE_NAME_MAX_SIZE, "%s", fg);
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
    if (!bypass_state || !power_state) return;

    if (stop_requested) {
        int bypass_still_active = sync_bypass_supply(bypass_state, 0,
                                                     current_max_file, current_max_file_num,
                                                     current_limit_file, current_limit_file_num,
                                                     normal_current);

        if (!power_state->stop_applied) {
            if (bypass_still_active) {
                printf_with_time("硬件旁路暂未恢复原值，仍优先执行停止充电");
            }
            charge_ctl("0");
            power_state->stop_applied = 1;
        }
        return;
    }

    if (power_state->stop_applied) {
        charge_ctl("1");
        power_state->stop_applied = 0;
    }

    sync_bypass_supply(bypass_state, bypass_requested,
                       current_max_file, current_max_file_num,
                       current_limit_file, current_limit_file_num,
                       normal_current);
}

void restore_charge_control(BypassState *bypass_state,
                            PowerControlState *power_state,
                            char **current_max_file,
                            int current_max_file_num,
                            char **current_limit_file,
                            int current_limit_file_num,
                            const char *normal_current)
{
    if (bypass_state) {
        for (int attempt = 0; attempt < BYPASS_RESTORE_RETRIES; attempt++) {
            int bypass_still_active = sync_bypass_supply(bypass_state, 0,
                                                         current_max_file, current_max_file_num,
                                                         current_limit_file, current_limit_file_num,
                                                         normal_current);
            if (!bypass_still_active || bypass_state->mode != BYPASS_MODE_HARDWARE)
                break;
            if (attempt + 1 < BYPASS_RESTORE_RETRIES)
                usleep(BYPASS_RESTORE_DELAY_US);
        }

        if (bypass_state->mode == BYPASS_MODE_HARDWARE) {
            printf_with_time("退出时硬件旁路仍未恢复，节点=%s；请在下次运行前确认设备节点可写",
                             bypass_state->node_path);
        }
    }

    if (power_state && power_state->stop_applied) {
        charge_ctl("1");
        power_state->stop_applied = 0;
    }
}

void apply_step_charge_policy(uchar step_charge, const char *power)
{
    if (step_charge == 1) {
        if (read_one_option("STEP_CHARGING_DISABLED") == 1) {
            if (atoi(power) < read_one_option("STEP_CHARGING_DISABLED_THRESHOLD"))
                step_charge_ctl("1");
            else
                step_charge_ctl("0");
        } else {
            step_charge_ctl("1");
        }
    } else if (step_charge == 2) {
        if (read_one_option("STEP_CHARGING_DISABLED") == 1)
            step_charge_ctl("0");
        else
            step_charge_ctl("1");
    }
}
