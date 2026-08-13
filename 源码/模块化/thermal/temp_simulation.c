#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "printf_with_time.h"
#include "read_option_file.h"
#include "temp_simulation.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int guess_temp_unit(const char *path)
{
    char buf[32] = {0};

    if (!read_file(path, buf, sizeof(buf))) {
        return 1000;
    }

    int v = 0;
    if (!parse_non_negative_int(buf, &v)) {
        return 1000;
    }

    if (v < 0) v = -v;

    if (v >= 1000 || v <= 100) {
        return 1000;
    }

    return 10;
}

static int normalize_temp_to_mc(int raw, int unit)
{
    if (raw < 0) raw = -raw;

    if (unit == 10) {
        return raw * 100;
    }

    if (raw <= 100) {
        return raw * 1000;
    }

    return raw;
}

int read_temp_mc(const char *path, int *out)
{
    if (!path || !out) return 0;

    char buf[32] = {0};
    int raw = 0;

    if (!read_file(path, buf, sizeof(buf))) return 0;
    if (!parse_non_negative_int(buf, &raw)) return 0;

    *out = normalize_temp_to_mc(raw, guess_temp_unit(path));
    return 1;
}

static void format_temp_value(int deg_c, int unit, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;

    if (unit == 1000) {
        snprintf(out, out_size, "%d", deg_c * 1000);
    } else {
        snprintf(out, out_size, "%d", deg_c * 10);
    }
}

static void add_temp_fake_node(TempSimState *st, const char *target, const char *label)
{
    if (!st || !target || !*target) return;
    if (!file_exists(target)) return;
    if (st->count >= TEMP_NODE_MAX) return;

    for (int i = 0; i < st->count; i++) {
        if (strcmp(st->nodes[i].target, target) == 0) {
            return;
        }
    }

    TempFakeNode *n = &st->nodes[st->count];

    snprintf(n->target, sizeof(n->target), "%s", target);
    snprintf(n->fake, sizeof(n->fake), STATE_DIR "/fake_temp_%d", st->count);
    snprintf(n->label, sizeof(n->label), "%s", label ? label : "battery_temp");

    n->unit = guess_temp_unit(target);
    n->mounted = 0;

    st->count++;
}

static void discover_battery_temp_nodes(TempSimState *st)
{
    if (!st) return;
    if (st->discovered) return;

    st->discovered = 1;
    st->count = 0;

    add_temp_fake_node(st,
                       "/sys/class/power_supply/battery/temp",
                       "power_supply:battery");

    add_temp_fake_node(st,
                       "/sys/class/power_supply/bms/temp",
                       "power_supply:bms");

    char **ps_dirs = NULL;
    int ps_num = list_dir("/sys/class/power_supply", &ps_dirs);

    for (int i = 0; i < ps_num; i++) {
        if (!ps_dirs[i]) continue;

        const char *base = strrchr(ps_dirs[i], '/');
        base = base ? base + 1 : ps_dirs[i];

        char type_path[PATH_MAX] = {0};
        char temp_path[PATH_MAX] = {0};
        char type[128] = {0};

        snprintf(type_path, sizeof(type_path), "%s/type", ps_dirs[i]);
        snprintf(temp_path, sizeof(temp_path), "%s/temp", ps_dirs[i]);

        /* type 读不到时仍可以根据目录名判断，因此不作为错误处理。 */
        read_file(type_path, type, sizeof(type));

        int is_batt =
            contains_ignore_case(base, "battery") ||
            contains_ignore_case(base, "batt") ||
            contains_ignore_case(base, "bms") ||
            contains_ignore_case(type, "Battery") ||
            contains_ignore_case(type, "BMS");

        if (is_batt) {
            char label[128] = {0};
            snprintf(label, sizeof(label), "power_supply:%s", base);
            add_temp_fake_node(st, temp_path, label);
        }
    }

    free_string_array(&ps_dirs, ps_num);

    char **thermal_dirs = NULL;
    int thermal_num = list_dir("/sys/class/thermal", &thermal_dirs);

    for (int i = 0; i < thermal_num; i++) {
        if (!thermal_dirs[i]) continue;
        if (!strstr(thermal_dirs[i], "thermal_zone")) continue;

        char type_path[PATH_MAX] = {0};
        char temp_path[PATH_MAX] = {0};
        char type[128] = {0};

        snprintf(type_path, sizeof(type_path), "%s/type", thermal_dirs[i]);
        snprintf(temp_path, sizeof(temp_path), "%s/temp", thermal_dirs[i]);

        if (!read_file(type_path, type, sizeof(type))) continue;

        int is_batt =
            contains_ignore_case(type, "battery") ||
            contains_ignore_case(type, "batt") ||
            contains_ignore_case(type, "bms");

        if (is_batt) {
            char label[128] = {0};
            snprintf(label, sizeof(label), "thermal:%s", type);
            add_temp_fake_node(st, temp_path, label);
        }
    }

    free_string_array(&thermal_dirs, thermal_num);

    printf_with_time("温度模拟：发现 %d 个电池温度相关节点", st->count);

    for (int i = 0; i < st->count; i++) {
        printf_with_time("温度模拟节点：%s，单位系数=%d，路径=%s",
                         st->nodes[i].label,
                         st->nodes[i].unit,
                         st->nodes[i].target);
    }
}

static int restore_battery_temp_nodes(TempSimState *st)
{
    int restored = 0;

    for (int i = 0; i < st->count; i++) {
        TempFakeNode *n = &st->nodes[i];

        if (n->mounted) {
            if (unbind_mount_file(n->target) > 0) {
                restored++;
            }
            n->mounted = 0;
        }

        char zero_text[32] = {0};
        format_temp_value(0, n->unit, zero_text, sizeof(zero_text));
        if (write_text_file(n->target, zero_text)) {
            restored++;
        }

        if (n->fake[0] && file_exists(n->fake)) {
            unlink(n->fake);
        }
    }

    return restored;
}

static int apply_one_temp_node(TempFakeNode *n, const char *temp_text)
{
    if (!n || !temp_text) return 0;

    if (n->mounted) {
        if (write_text_file(n->fake, temp_text)) {
            return 1;
        }

        unbind_mount_file(n->target);
        n->mounted = 0;
    }

    if (write_text_file(n->target, temp_text)) {
        return 1;
    }

    ensure_dir(STATE_DIR);

    if (!write_text_file(n->fake, temp_text)) {
        return 0;
    }

    if (bind_mount_file(n->fake, n->target)) {
        n->mounted = 1;
        return 1;
    }

    unlink(n->fake);
    return 0;
}

int cleanup_battery_temp_simulation(TempSimState *st)
{
    if (!st) return 0;

    discover_battery_temp_nodes(st);

    if (st->last_simulating != 1) return 0;

    int restored = restore_battery_temp_nodes(st);

    if (restored > 0) {
        printf_with_time("已清除温度伪装值，共 %d 个节点（内核会自动恢复实际温度）", restored);
    }

    st->last_simulating = 0;
    return restored;
}

int apply_battery_temp_simulation(TempSimState *st, int is_charging)
{
    if (!st) return -1;

    int enable = read_one_option("TEMP_SIMULATE");
    int mode = read_one_option("TEMP_SIMULATE_MOUNT_MODE");
    int value = read_one_option("TEMP_SIMULATE_VALUE");

    if (mode != 0 && mode != 1) mode = 0;
    if (value < 0) value = 0;
    if (value > 100) value = 100;

    discover_battery_temp_nodes(st);

    int simulating = (enable == 1) && (mode == 0 || is_charging);

    if (!simulating) {
        if (st->last_simulating == 1) {
            restore_battery_temp_nodes(st);
            if (enable != 1)
                printf_with_time("温度模拟已关闭，已清除伪装值（内核会自动恢复实际温度）");
            else
                printf_with_time("温度模拟为仅充电模式，当前未充电，已清除伪装值");
        }

        st->last_value = value;
        st->last_simulating = 0;

        return -1;
    }

    if (st->count <= 0) {
        if (st->last_simulating != 1) {
            printf_with_time("温度模拟开启，但没有发现可写入的电池温度节点");
        }

        st->last_value = value;
        st->last_simulating = 1;

        return value * 1000;
    }

    int ok = 0;
    int mounted = 0;
    for (int i = 0; i < st->count; i++) {
        TempFakeNode *n = &st->nodes[i];
        char temp_text[32] = {0};
        format_temp_value(value, n->unit, temp_text, sizeof(temp_text));

        if (apply_one_temp_node(n, temp_text)) {
            ok++;
            if (n->mounted) mounted++;
        }
    }

    if (st->last_simulating != 1 || st->last_value != value) {
        printf_with_time("温度模拟已开启，目标温度 %d℃，成功生效 %d/%d 个节点，其中挂载兜底 %d 个",
                         value, ok, st->count, mounted);
    }

    if (ok == 0 && st->last_sim_all_failed != 1) {
        printf_with_time("温度模拟所有节点写入与挂载均失败，伪装温度未生效");
        st->last_sim_all_failed = 1;
    } else if (ok > 0) {
        st->last_sim_all_failed = 0;
    }

    st->last_value = value;
    st->last_simulating = 1;

    return value * 1000;
}

int current_simulated_temp_mc(void)
{
    if (read_one_option("TEMP_SIMULATE") != 1) {
        return -1;
    }

    int value = read_one_option("TEMP_SIMULATE_VALUE");

    if (value < 0) value = 0;
    if (value > 100) value = 100;

    return value * 1000;
}

void handle_option_generation_change(unsigned long *last_generation,
                                            TempSimState *temp_sim_state,
                                            int is_charging)
{
    if (!last_generation || !temp_sim_state) return;

    unsigned long cur_generation = read_option_generation();

    if (cur_generation != *last_generation) {
        *last_generation = cur_generation;
        apply_battery_temp_simulation(temp_sim_state, is_charging);
    }
}
