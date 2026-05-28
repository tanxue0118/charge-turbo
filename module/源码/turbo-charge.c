#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <pthread.h>
#include <errno.h>
#include <ctype.h>
#include <sys/inotify.h>
#include <sys/mount.h>

#define BYPASS_CHARGE_CURRENT "500000"
#define APP_PACKAGE_NAME_MAX_SIZE 100
#define OPTION_NAME_MAX_SIZE 64
#define LOG_BUF_SIZE 1024

#define MODDIR_PATH "/data/adb/modules/turbo-charge"
#define STATE_DIR MODDIR_PATH "/state"
#define TEMP_NODE_MAX 64

typedef unsigned char uchar;

typedef struct {
    const char *name;
    int value;
    int default_value;
} Option;

typedef struct {
    char target[PATH_MAX];
    char mount_target[PATH_MAX];
    char fake[PATH_MAX];
    char label[128];
    int unit;
    int mounted;
} TempFakeNode;

typedef struct {
    TempFakeNode nodes[TEMP_NODE_MAX];
    int count;
    int discovered;
    int last_enable;
    int last_value;
    int last_ok;
} TempSimState;

static const char option_dir[] = MODDIR_PATH;
static const char option_name[] = "option.txt";
static const char option_file[] = MODDIR_PATH "/option.txt";
static const char bypass_charge_file[] = MODDIR_PATH "/bypass_charge.txt";

static const char *temp_sensors[] = {
    "battery",
    "battery-high",
    "battery-low",
    "batt_therm",
    "battery_therm",
    "shell_front",
    "shell_frame",
    "shell_back",
    "skin-msm-therm",
    "virt-front-therm",
    "virt-back-therm",
    "virt-frame-therm",
    "quiet_therm",
    "quiet-therm",
    "xo_therm",
    "xo-therm",
    "conn_therm",
    "wifi_therm",
    "modem_therm",
    "modem-skin-usr",
    "usb",
    "usb-user",
    "usb-therm",
    "mtktsbtsnrpa",
    "lcd_therm",
    "mtktsbtsmdpa",
    "mtktsAP",
    "modem-0-usr",
    "modem1_wifi",
    "ddr-usr",
    "cwlan-usr"
};

static Option options[] = {
    {"CYCLE_TIME", 1, 1},
    {"CURRENT_MAX", 50000000, 50000000},
    {"STEP_CHARGING_DISABLED", 0, 0},
    {"TEMP_CTRL", 1, 1},
    {"POWER_CTRL", 0, 0},
    {"STEP_CHARGING_DISABLED_THRESHOLD", 15, 15},
    {"CHARGE_STOP", 95, 95},
    {"CHARGE_START", 80, 80},
    {"TEMP_MAX", 52, 52},
    {"HIGHEST_TEMP_CURRENT", 2000000, 2000000},
    {"RECHARGE_TEMP", 45, 45},
    {"TEMP_SIMULATE", 0, 0},
    {"TEMP_SIMULATE_VALUE", 28, 28},
    {"BYPASS_CHARGE", 0, 0}
};

static const int option_count = sizeof(options) / sizeof(options[0]);

static pthread_mutex_t mutex_options = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_foreground_app = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_thread = PTHREAD_MUTEX_INITIALIZER;

static unsigned long option_generation = 0;

static char foreground_app_name[APP_PACKAGE_NAME_MAX_SIZE] = {0};
static int foreground_thread_running = 0;
static int foreground_thread_stop = 0;

static void printf_with_time(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void line_feed(char *line)
{
    if (!line) return;

    char *p = strchr(line, '\r');
    if (p) *p = '\0';

    p = strchr(line, '\n');
    if (p) *p = '\0';
}

static void get_utc8_time(struct tm *ptm)
{
    time_t cur_time = time(NULL);
    cur_time += 8 * 3600;
    gmtime_r(&cur_time, ptm);

    ptm->tm_year += 1900;
    ptm->tm_mon += 1;
}

static void printf_with_time(const char *format, ...)
{
    char buffer[LOG_BUF_SIZE] = {0};
    struct tm time_now;
    va_list ap;

    va_start(ap, format);
    vsnprintf(buffer, sizeof(buffer), format, ap);
    va_end(ap);

    get_utc8_time(&time_now);

    printf("[ %04d.%02d.%02dT%02d:%02d:%02d UTC+8 ] %s\n",
           time_now.tm_year,
           time_now.tm_mon,
           time_now.tm_mday,
           time_now.tm_hour,
           time_now.tm_min,
           time_now.tm_sec,
           buffer);

    fflush(stdout);
}

static int file_exists(const char *file)
{
    return access(file, F_OK) == 0;
}

static int file_readable(const char *file)
{
    return access(file, R_OK) == 0;
}

static int ensure_readable(const char *file)
{
    if (!file_exists(file)) return 0;

    if (!file_readable(file)) {
        chmod(file, 0644);
    }

    return file_readable(file);
}

static int read_file(const char *file_path, char *buf, size_t buf_size)
{
    if (!file_path || !buf || buf_size == 0) return 0;

    buf[0] = '\0';

    if (!ensure_readable(file_path)) return 0;

    FILE *fp = fopen(file_path, "r");
    if (!fp) return 0;

    if (!fgets(buf, buf_size, fp)) {
        fclose(fp);
        buf[0] = '\0';
        return 0;
    }

    fclose(fp);
    line_feed(buf);
    return 1;
}

static void ensure_dir(const char *dir)
{
    if (!dir) return;
    mkdir(dir, 0755);
    chmod(dir, 0755);
}

static void resolve_mount_target(const char *path, char *out, size_t out_size)
{
    if (!path || !out || out_size == 0) return;

    out[0] = '\0';

    char resolved[PATH_MAX] = {0};

    if (realpath(path, resolved)) {
        snprintf(out, out_size, "%s", resolved);
    } else {
        snprintf(out, out_size, "%s", path);
    }
}

static void set_value(const char *file, const char *value)
{
    if (!file || !value) return;
    if (!file_exists(file)) return;

    FILE *fp = fopen(file, "r+");

    if (!fp) {
        chmod(file, 0644);
        fp = fopen(file, "r+");
    }

    if (!fp) return;

    char old[128] = {0};

    if (fgets(old, sizeof(old), fp)) {
        line_feed(old);
    }

    if (strcmp(old, value) != 0) {
        rewind(fp);
        ftruncate(fileno(fp), 0);
        fputs(value, fp);
        fflush(fp);
    }

    fclose(fp);
}

static void set_array_value(char **files, int num, const char *value)
{
    if (!files || num <= 0 || !value) return;

    for (int i = 0; i < num; i++) {
        if (files[i]) set_value(files[i], value);
    }
}

static void free_string_array(char ***arr, int num)
{
    if (!arr || !*arr) return;

    for (int i = 0; i < num; i++) {
        free((*arr)[i]);
        (*arr)[i] = NULL;
    }

    free(*arr);
    *arr = NULL;
}

static int list_dir(const char *path, char ***out)
{
    if (!path || !out) return 0;

    DIR *dir = opendir(path);
    if (!dir) {
        *out = NULL;
        return 0;
    }

    int count = 0;
    int cap = 16;
    char **list = calloc(cap, sizeof(char *));

    if (!list) {
        closedir(dir);
        *out = NULL;
        return 0;
    }

    struct dirent *ent;

    while ((ent = readdir(dir)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;

        if (count >= cap) {
            cap *= 2;
            char **tmp = realloc(list, sizeof(char *) * cap);
            if (!tmp) break;
            list = tmp;
        }

        size_t len = strlen(path) + strlen(ent->d_name) + 2;
        list[count] = calloc(1, len);
        if (!list[count]) break;

        snprintf(list[count], len, "%s/%s", path, ent->d_name);
        count++;
    }

    closedir(dir);

    if (count == 0) {
        free(list);
        list = NULL;
    } else {
        char **tmp = realloc(list, sizeof(char *) * count);
        if (tmp) list = tmp;
    }

    *out = list;
    return count;
}

static int option_index(const char *name)
{
    for (int i = 0; i < option_count; i++) {
        if (strcmp(options[i].name, name) == 0) return i;
    }

    return -1;
}

static int read_one_option(const char *name)
{
    int idx = option_index(name);

    if (idx < 0) {
        printf_with_time("内部错误：无法获取配置项 %s", name);
        exit(98765);
    }

    pthread_mutex_lock(&mutex_options);
    int value = options[idx].value;
    pthread_mutex_unlock(&mutex_options);

    return value;
}

static unsigned long read_option_generation(void)
{
    pthread_mutex_lock(&mutex_options);
    unsigned long gen = option_generation;
    pthread_mutex_unlock(&mutex_options);

    return gen;
}

static int parse_non_negative_int(const char *str, int *out)
{
    if (!str || !*str || !out) return 0;

    for (const char *p = str; *p; p++) {
        if (!isdigit((unsigned char)*p)) return 0;
    }

    errno = 0;
    long v = strtol(str, NULL, 10);

    if (errno != 0) return 0;
    if (v < 0 || v > INT_MAX) return 0;

    *out = (int)v;
    return 1;
}

static void load_option_file(int first_run)
{
    FILE *fp = fopen(option_file, "r");

    if (!fp) {
        if (first_run)
            printf_with_time("无法打开配置文件 %s，使用默认配置", option_file);
        else
            printf_with_time("无法打开配置文件 %s，沿用上一次配置", option_file);
        return;
    }

    int found[option_count];
    memset(found, 0, sizeof(found));

    char line[256];

    pthread_mutex_lock(&mutex_options);

    while (fgets(line, sizeof(line), fp)) {
        line_feed(line);

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '\0' || *p == '#') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        char *name = p;
        char *value_str = eq + 1;

        int idx = option_index(name);
        if (idx < 0) continue;

        found[idx] = 1;

        int new_value = 0;

        if (!parse_non_negative_int(value_str, &new_value)) {
            if (first_run)
                printf_with_time("配置文件 %s 的值为空、非纯数字或超过范围，使用默认值 %d",
                                 options[idx].name, options[idx].value);
            else
                printf_with_time("%s 的值非法，沿用上一次的值 %d",
                                 options[idx].name, options[idx].value);
            continue;
        }

        if (strcmp(options[idx].name, "CYCLE_TIME") == 0 && new_value == 0) {
            if (first_run)
                printf_with_time("CYCLE_TIME 的值为 0，这是不允许的，使用默认值 %d",
                                 options[idx].value);
            else
                printf_with_time("CYCLE_TIME 的值为 0，这是不允许的，沿用上一次的值 %d",
                                 options[idx].value);
            continue;
        }

        if (options[idx].value != new_value) {
            options[idx].value = new_value;

            if (!first_run)
                printf_with_time("%s 的值更改为 %d", options[idx].name, options[idx].value);
        }
    }

    for (int i = 0; i < option_count; i++) {
        if (!found[i]) {
            if (first_run)
                printf_with_time("配置文件中不存在 %s，使用默认值 %d",
                                 options[i].name, options[i].value);
            else
                printf_with_time("配置文件中不存在 %s，沿用上一次的值 %d",
                                 options[i].name, options[i].value);
        }
    }

    option_generation++;

    pthread_mutex_unlock(&mutex_options);

    fclose(fp);
}

static int is_option_event(struct inotify_event *ev)
{
    if (!ev) return 0;

    if (ev->len > 0 && ev->name[0]) {
        if (strcmp(ev->name, option_name) != 0) {
            return 0;
        }
    }

    if (ev->mask & IN_CLOSE_WRITE) return 1;
    if (ev->mask & IN_MOVED_TO) return 1;
    if (ev->mask & IN_CREATE) return 1;
    if (ev->mask & IN_ATTRIB) return 1;

    return 0;
}

static void *read_option_file_thread(void *arg)
{
    (void)arg;

    if (file_exists(option_file)) {
        load_option_file(1);
    } else {
        printf_with_time("找不到配置文件 %s，使用内置默认配置", option_file);
    }

    int fd = inotify_init1(IN_CLOEXEC);

    if (fd < 0) {
        printf_with_time("inotify 初始化失败，配置文件实时监听不可用：%s", strerror(errno));
        return NULL;
    }

    int wd = inotify_add_watch(fd,
                               option_dir,
                               IN_CLOSE_WRITE |
                               IN_MOVED_TO |
                               IN_CREATE |
                               IN_ATTRIB |
                               IN_DELETE_SELF |
                               IN_MOVE_SELF);

    if (wd < 0) {
        printf_with_time("inotify 监听目录失败：%s，目录：%s", strerror(errno), option_dir);
        close(fd);
        return NULL;
    }

    printf_with_time("配置文件监听已启动：%s", option_file);

    char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));

    while (1) {
        ssize_t len = read(fd, buf, sizeof(buf));

        if (len < 0) {
            if (errno == EINTR) continue;

            printf_with_time("inotify 读取事件失败：%s", strerror(errno));
            break;
        }

        if (len == 0) continue;

        int need_reload = 0;
        int need_rewatch = 0;

        for (char *ptr = buf; ptr < buf + len; ) {
            struct inotify_event *ev = (struct inotify_event *)ptr;

            if (ev->mask & IN_Q_OVERFLOW) {
                need_reload = 1;
            }

            if (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED)) {
                need_rewatch = 1;
            }

            if (is_option_event(ev)) {
                need_reload = 1;
            }

            ptr += sizeof(struct inotify_event) + ev->len;
        }

        if (need_reload) {
            if (file_exists(option_file)) {
                load_option_file(0);
            } else {
                printf_with_time("配置文件事件触发，但文件不存在：%s", option_file);
            }
        }

        if (need_rewatch) {
            inotify_rm_watch(fd, wd);

            wd = inotify_add_watch(fd,
                                   option_dir,
                                   IN_CLOSE_WRITE |
                                   IN_MOVED_TO |
                                   IN_CREATE |
                                   IN_ATTRIB |
                                   IN_DELETE_SELF |
                                   IN_MOVE_SELF);

            if (wd < 0) {
                printf_with_time("重新添加 inotify 监听失败：%s", strerror(errno));
                break;
            }

            printf_with_time("配置文件监听已重新建立：%s", option_file);
        }
    }

    if (wd >= 0) {
        inotify_rm_watch(fd, wd);
    }

    close(fd);

    return NULL;
}

static int contains_ignore_case(const char *s, const char *sub)
{
    if (!s || !sub) return 0;

    size_t sub_len = strlen(sub);
    if (sub_len == 0) return 1;

    for (const char *p = s; *p; p++) {
        size_t i = 0;

        while (i < sub_len &&
               p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)sub[i])) {
            i++;
        }

        if (i == sub_len) return 1;
    }

    return 0;
}

static int ends_with(const char *s, const char *suffix)
{
    if (!s || !suffix) return 0;

    size_t s_len = strlen(s);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > s_len) return 0;

    return strcmp(s + s_len - suffix_len, suffix) == 0;
}

static int write_text_file(const char *path, const char *text)
{
    if (!path || !text) return 0;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

    if (fd < 0) {
        return 0;
    }

    size_t len = strlen(text);
    ssize_t ret = write(fd, text, len);

    fsync(fd);
    close(fd);

    chmod(path, 0644);

    return ret == (ssize_t)len;
}

static int is_path_mounted(const char *target)
{
    if (!target || !*target) return 0;

    char resolved[PATH_MAX] = {0};
    resolve_mount_target(target, resolved, sizeof(resolved));

    FILE *fp = fopen("/proc/self/mountinfo", "r");
    if (!fp) return 0;

    char line[4096];

    while (fgets(line, sizeof(line), fp)) {
        char tmp[4096];
        snprintf(tmp, sizeof(tmp), "%s", line);

        char *saveptr = NULL;
        char *tok = strtok_r(tmp, " ", &saveptr);
        int field = 0;
        char *mount_point = NULL;

        while (tok) {
            field++;

            if (field == 5) {
                mount_point = tok;
                break;
            }

            tok = strtok_r(NULL, " ", &saveptr);
        }

        if (!mount_point) continue;

        if (strcmp(mount_point, target) == 0 ||
            strcmp(mount_point, resolved) == 0) {
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

static int bind_mount_file(const char *fake, const char *target)
{
    if (!fake || !target) return 0;
    if (!file_exists(fake) || !file_exists(target)) return 0;

    char resolved[PATH_MAX] = {0};
    resolve_mount_target(target, resolved, sizeof(resolved));

    if (is_path_mounted(target) || is_path_mounted(resolved)) {
        return 1;
    }

    if (mount(fake, resolved, NULL, MS_BIND, NULL) == 0) {
        return 1;
    }

    if (strcmp(resolved, target) != 0) {
        if (mount(fake, target, NULL, MS_BIND, NULL) == 0) {
            return 1;
        }
    }

    return 0;
}

static int unbind_mount_file(const char *target)
{
    if (!target || !*target) return 0;

    char resolved[PATH_MAX] = {0};
    resolve_mount_target(target, resolved, sizeof(resolved));

    int count = 0;

    for (int i = 0; i < 16; i++) {
        int did = 0;

        if (is_path_mounted(resolved)) {
            if (umount2(resolved, MNT_DETACH) == 0) {
                count++;
                did = 1;
                usleep(50000);
            } else {
                printf_with_time("解除真实温度节点挂载失败：%s，原因：%s",
                                 resolved,
                                 strerror(errno));
            }
        }

        if (strcmp(resolved, target) != 0 && is_path_mounted(target)) {
            if (umount2(target, MNT_DETACH) == 0) {
                count++;
                did = 1;
                usleep(50000);
            } else {
                printf_with_time("解除温度节点挂载失败：%s，原因：%s",
                                 target,
                                 strerror(errno));
            }
        }

        if (!did) {
            break;
        }
    }

    return count;
}

static int guess_temp_unit(const char *path)
{
    char buf[32] = {0};

    if (!read_file(path, buf, sizeof(buf))) {
        return 10;
    }

    int v = atoi(buf);
    if (v < 0) v = -v;

    if (v >= 1000) {
        return 1000;
    }

    return 10;
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

    char resolved[PATH_MAX] = {0};
    resolve_mount_target(target, resolved, sizeof(resolved));

    for (int i = 0; i < st->count; i++) {
        if (strcmp(st->nodes[i].target, target) == 0 ||
            strcmp(st->nodes[i].mount_target, resolved) == 0) {
            return;
        }
    }

    TempFakeNode *n = &st->nodes[st->count];

    snprintf(n->target, sizeof(n->target), "%s", target);
    snprintf(n->mount_target, sizeof(n->mount_target), "%s", resolved);
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

    ensure_dir(STATE_DIR);

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
        printf_with_time("温度模拟节点：%s，单位系数=%d，原始路径=%s，真实路径=%s",
                         st->nodes[i].label,
                         st->nodes[i].unit,
                         st->nodes[i].target,
                         st->nodes[i].mount_target);
    }
}

static int cleanup_battery_temp_simulation(TempSimState *st)
{
    if (!st) return 0;

    discover_battery_temp_nodes(st);

    int total_unmounted = 0;

    for (int i = 0; i < st->count; i++) {
        TempFakeNode *n = &st->nodes[i];

        if (!n->target[0]) continue;

        int unmounted_a = unbind_mount_file(n->mount_target);
        int unmounted_b = 0;

        if (strcmp(n->mount_target, n->target) != 0) {
            unmounted_b = unbind_mount_file(n->target);
        }

        int n_unmounted = unmounted_a + unmounted_b;

        total_unmounted += n_unmounted;

        if (n_unmounted > 0) {
            printf_with_time("已解除温度节点挂载：原始路径=%s，真实路径=%s，共解除 %d 层",
                             n->target,
                             n->mount_target,
                             n_unmounted);
        }

        n->mounted = 0;
    }

    return total_unmounted;
}

static int apply_battery_temp_simulation(TempSimState *st)
{
    if (!st) return -1;

    int enable = read_one_option("TEMP_SIMULATE");
    int value = read_one_option("TEMP_SIMULATE_VALUE");

    if (value < 0) value = 0;
    if (value > 100) value = 100;

    if (enable != 1) {
        int unmounted = cleanup_battery_temp_simulation(st);

        if (st->last_enable == 1 || unmounted > 0) {
            printf_with_time("温度模拟已关闭，已解除 %d 层温度节点挂载",
                             unmounted);
        } else if (st->last_enable != 0) {
            printf_with_time("温度模拟处于关闭状态，未发现需要解除的挂载");
        }

        st->last_enable = 0;
        st->last_value = value;
        st->last_ok = 0;

        return -1;
    }

    discover_battery_temp_nodes(st);

    if (st->count <= 0) {
        if (st->last_enable != 1 || st->last_ok != 0) {
            printf_with_time("温度模拟开启，但没有发现可伪装的电池温度节点");
        }

        st->last_enable = 1;
        st->last_value = value;
        st->last_ok = 0;

        return value * 1000;
    }

    int ok = 0;

    for (int i = 0; i < st->count; i++) {
        TempFakeNode *n = &st->nodes[i];

        char temp_text[32] = {0};
        format_temp_value(value, n->unit, temp_text, sizeof(temp_text));

        if (!write_text_file(n->fake, temp_text)) {
            printf_with_time("写入假温度文件失败：%s", n->fake);
            continue;
        }

        if (bind_mount_file(n->fake, n->mount_target)) {
            n->mounted = 1;
            ok++;
        } else {
            printf_with_time("温度节点 bind mount 失败：%s -> %s，原因：%s",
                             n->fake,
                             n->mount_target,
                             strerror(errno));
        }
    }

    if (st->last_enable != 1 ||
        st->last_value != value ||
        st->last_ok != ok) {
        printf_with_time("温度模拟已开启，目标温度 %d℃，成功伪装 %d/%d 个电池温度节点",
                         value,
                         ok,
                         st->count);

        if (ok == 0) {
            printf_with_time("温度模拟失败：bind mount 未成功，可能是内核、SELinux 或挂载命名空间限制");
        }
    }

    st->last_enable = 1;
    st->last_value = value;
    st->last_ok = ok;

    return value * 1000;
}

static int current_simulated_temp_mc(void)
{
    if (read_one_option("TEMP_SIMULATE") != 1) {
        return -1;
    }

    int value = read_one_option("TEMP_SIMULATE_VALUE");

    if (value < 0) value = 0;
    if (value > 100) value = 100;

    return value * 1000;
}

static void handle_option_generation_change(unsigned long *last_generation,
                                            TempSimState *temp_sim_state)
{
    if (!last_generation || !temp_sim_state) return;

    unsigned long cur_generation = read_option_generation();

    if (cur_generation != *last_generation) {
        *last_generation = cur_generation;
        apply_battery_temp_simulation(temp_sim_state);
    }
}

static void step_charge_ctl(const char *value)
{
    set_value("/sys/class/power_supply/battery/step_charging_enabled", value);
    set_value("/sys/class/power_supply/battery/sw_jeita_enabled", value);
}

static void charge_ctl(const char *value)
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

static void power_ctl(int *last_charge_stop, int *charge_is_stop)
{
    int charge_stop = read_one_option("CHARGE_STOP");
    int charge_start = read_one_option("CHARGE_START");
    char power[16] = {0};

    if (*last_charge_stop == -1) *last_charge_stop = charge_stop;

    if (read_one_option("POWER_CTRL") != 1) {
        if (*charge_is_stop) {
            printf_with_time("电量控制关闭，恢复充电");
            *charge_is_stop = 0;
        }

        *last_charge_stop = charge_stop;
        charge_ctl("1");
        return;
    }

    if (!read_file("/sys/class/power_supply/battery/capacity", power, sizeof(power))) {
        printf_with_time("无法读取电量，跳过本次电量控制");
        return;
    }

    int power_int = atoi(power);

    if (*last_charge_stop != charge_stop && *charge_is_stop) {
        if (power_int < charge_stop) {
            printf_with_time("当前电量 %d%%，小于新的停止阈值，恢复充电", power_int);
            *charge_is_stop = 0;
            charge_ctl("1");
        } else {
            printf_with_time("当前电量 %d%%，大于等于新的停止阈值，保持停止充电", power_int);
            charge_ctl("0");
        }

        *last_charge_stop = charge_stop;
    }

    if (power_int >= charge_stop && !*charge_is_stop) {
        printf_with_time("当前电量 %d%%，大于等于停止阈值，停止充电", power_int);
        *charge_is_stop = 1;
        charge_ctl("0");
    }

    if (power_int <= charge_start && *charge_is_stop) {
        printf_with_time("当前电量 %d%%，小于等于恢复阈值，恢复充电", power_int);
        *charge_is_stop = 0;
        charge_ctl("1");
    }
}

static int check_android_version(void)
{
    char version[32] = {0};
    FILE *fp = popen("getprop ro.build.version.release", "r");

    if (!fp) {
        printf_with_time("无法获取安卓版本，伪旁路供电功能失效");
        return 0;
    }

    if (!fgets(version, sizeof(version), fp)) {
        pclose(fp);
        printf_with_time("无法读取安卓版本，伪旁路供电功能失效");
        return 0;
    }

    pclose(fp);
    line_feed(version);

    int android_version = atoi(version);

    if (android_version < 7) {
        printf_with_time("安卓版本低于 7，无法可靠获取前台应用，伪旁路供电功能失效");
        return 0;
    }

    return android_version;
}

static void set_foreground_app(const char *name)
{
    pthread_mutex_lock(&mutex_foreground_app);
    snprintf(foreground_app_name, sizeof(foreground_app_name), "%s", name ? name : "");
    pthread_mutex_unlock(&mutex_foreground_app);
}

static void get_foreground_app(char *out, size_t out_size)
{
    pthread_mutex_lock(&mutex_foreground_app);
    snprintf(out, out_size, "%s", foreground_app_name);
    pthread_mutex_unlock(&mutex_foreground_app);
}

static int should_stop_foreground_thread(void)
{
    pthread_mutex_lock(&mutex_thread);
    int stop = foreground_thread_stop;
    pthread_mutex_unlock(&mutex_thread);

    return stop;
}

static void *foreground_thread_func(void *arg)
{
    int android_version = *(int *)arg;
    free(arg);

    while (!should_stop_foreground_thread() && read_one_option("BYPASS_CHARGE") == 1) {
        char result[256] = {0};
        FILE *fp = popen("dumpsys deviceidle | grep 'mScreenOn'", "r");

        if (!fp) {
            printf_with_time("无法执行 dumpsys deviceidle");
            sleep(5);
            continue;
        }

        fgets(result, sizeof(result), fp);
        pclose(fp);
        line_feed(result);

        char *eq = strchr(result, '=');

        if (!eq) {
            printf_with_time("无法获取屏幕状态");
            sleep(5);
            continue;
        }

        if (strcmp(eq + 1, "true") != 0) {
            set_foreground_app("screen_is_off");
            sleep(5);
            continue;
        }

        memset(result, 0, sizeof(result));

        if (android_version < 10)
            fp = popen("dumpsys activity o | grep ' (top-activity)'", "r");
        else
            fp = popen("dumpsys activity lru | grep ' TOP'", "r");

        if (!fp) {
            printf_with_time("无法执行 dumpsys activity");
            sleep(5);
            continue;
        }

        fgets(result, sizeof(result), fp);
        pclose(fp);
        line_feed(result);

        char pkg[APP_PACKAGE_NAME_MAX_SIZE] = {0};

        if (android_version < 10) {
            char *top = strstr(result, "/TOP");
            if (!top) {
                printf_with_time("无法解析前台应用包名");
                sleep(5);
                continue;
            }

            char *colon1 = strchr(top, ':');
            if (!colon1) {
                printf_with_time("无法解析前台应用包名");
                sleep(5);
                continue;
            }

            char *colon2 = strchr(colon1 + 1, ':');
            if (!colon2) {
                printf_with_time("无法解析前台应用包名");
                sleep(5);
                continue;
            }

            char *slash = strchr(colon2 + 1, '/');
            if (!slash) {
                printf_with_time("无法解析前台应用包名");
                sleep(5);
                continue;
            }

            size_t len = (size_t)(slash - (colon2 + 1));
            if (len >= sizeof(pkg)) len = sizeof(pkg) - 1;
            memcpy(pkg, colon2 + 1, len);
        } else {
            char *top = strstr(result, " TOP");
            if (!top) {
                printf_with_time("无法解析前台应用包名");
                sleep(5);
                continue;
            }

            char *colon = strchr(top, ':');
            if (!colon) {
                printf_with_time("无法解析前台应用包名");
                sleep(5);
                continue;
            }

            char *slash = strchr(colon + 1, '/');
            if (!slash) {
                printf_with_time("无法解析前台应用包名");
                sleep(5);
                continue;
            }

            size_t len = (size_t)(slash - (colon + 1));
            if (len >= sizeof(pkg)) len = sizeof(pkg) - 1;
            memcpy(pkg, colon + 1, len);
        }

        if (pkg[0]) set_foreground_app(pkg);

        sleep(5);
    }

    set_foreground_app("");

    pthread_mutex_lock(&mutex_thread);
    foreground_thread_running = 0;
    foreground_thread_stop = 0;
    pthread_mutex_unlock(&mutex_thread);

    return NULL;
}

static void start_foreground_thread_if_needed(int android_version)
{
    pthread_mutex_lock(&mutex_thread);

    if (!foreground_thread_running) {
        foreground_thread_stop = 0;
        foreground_thread_running = 1;

        pthread_t tid;
        int *arg = malloc(sizeof(int));
        if (!arg) {
            foreground_thread_running = 0;
            pthread_mutex_unlock(&mutex_thread);
            return;
        }

        *arg = android_version;

        if (pthread_create(&tid, NULL, foreground_thread_func, arg) == 0) {
            pthread_detach(tid);
            printf_with_time("前台应用监听线程已启动");
        } else {
            free(arg);
            foreground_thread_running = 0;
            printf_with_time("前台应用监听线程启动失败");
        }
    }

    pthread_mutex_unlock(&mutex_thread);
}

static void stop_foreground_thread(void)
{
    pthread_mutex_lock(&mutex_thread);

    if (foreground_thread_running) {
        foreground_thread_stop = 1;
    }

    pthread_mutex_unlock(&mutex_thread);
}

static int load_bypass_app_list(char ***apps, int *app_num, time_t *last_mtime)
{
    struct stat st;

    if (stat(bypass_charge_file, &st) != 0) {
        return 0;
    }

    if (*apps && st.st_mtime == *last_mtime) {
        return 1;
    }

    if (*apps) {
        free_string_array(apps, *app_num);
        *app_num = 0;
    }

    *last_mtime = st.st_mtime;

    FILE *fp = fopen(bypass_charge_file, "r");
    if (!fp) return 0;

    char line[APP_PACKAGE_NAME_MAX_SIZE] = {0};
    int cap = 8;
    char **list = calloc(cap, sizeof(char *));

    if (!list) {
        fclose(fp);
        return 0;
    }

    int count = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_feed(line);

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '\0' || *p == '#') continue;

        if (count >= cap) {
            cap *= 2;
            char **tmp = realloc(list, sizeof(char *) * cap);
            if (!tmp) break;
            list = tmp;
        }

        list[count] = calloc(1, APP_PACKAGE_NAME_MAX_SIZE);
        if (!list[count]) break;

        snprintf(list[count], APP_PACKAGE_NAME_MAX_SIZE, "%s", p);
        count++;
    }

    fclose(fp);

    if (count == 0) {
        free(list);
        list = NULL;
    } else {
        char **tmp = realloc(list, sizeof(char *) * count);
        if (tmp) list = tmp;
    }

    *apps = list;
    *app_num = count;

    printf_with_time("伪旁路供电应用列表已重新读取，共 %d 个应用", count);

    return 1;
}

static void bypass_charge_ctl(int android_version,
                              char *last_appname,
                              int *is_bypass,
                              int *screen_is_off,
                              char **current_max_file,
                              int current_max_file_num)
{
    static char **bypass_apps = NULL;
    static int bypass_app_num = 0;
    static time_t bypass_file_last_mtime = 0;

    if (read_one_option("BYPASS_CHARGE") != 1) {
        if (*is_bypass) {
            printf_with_time("伪旁路供电关闭，恢复正常充电模式");
        }

        *is_bypass = 0;
        *screen_is_off = 0;
        last_appname[0] = '\0';
        stop_foreground_thread();
        return;
    }

    if (android_version <= 0) return;

    start_foreground_thread_if_needed(android_version);

    char fg[APP_PACKAGE_NAME_MAX_SIZE] = {0};
    get_foreground_app(fg, sizeof(fg));

    if (fg[0] == '\0') return;

    if (strcmp(fg, "screen_is_off") == 0) {
        if (!*screen_is_off) {
            if (*is_bypass)
                printf_with_time("手机屏幕关闭，暂时退出伪旁路供电模式");
            *screen_is_off = 1;
        }
        return;
    }

    if (*screen_is_off) {
        if (*is_bypass)
            printf_with_time("手机屏幕开启，恢复伪旁路供电判断");
        *screen_is_off = 0;
    }

    if (!load_bypass_app_list(&bypass_apps, &bypass_app_num, &bypass_file_last_mtime)) {
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
        if (!*is_bypass) {
            printf_with_time("当前前台应用 %s 位于伪旁路供电列表中，进入伪旁路供电模式", fg);
            *is_bypass = 1;
        } else if (strcmp(last_appname, fg) != 0) {
            printf_with_time("前台应用切换为 %s，保持伪旁路供电模式", fg);
        }

        set_array_value(current_max_file, current_max_file_num, BYPASS_CHARGE_CURRENT);
    } else {
        if (*is_bypass) {
            if (strcmp(last_appname, fg) != 0)
                printf_with_time("前台应用切换为 %s，不在伪旁路供电列表中，恢复正常充电模式", fg);
            else
                printf_with_time("%s 已不在伪旁路供电列表中，恢复正常充电模式", fg);

            *is_bypass = 0;
        }
    }

    snprintf(last_appname, APP_PACKAGE_NAME_MAX_SIZE, "%s", fg);
}

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

        for (int j = 0; j < (int)(sizeof(temp_sensors) / sizeof(temp_sensors[0])); j++) {
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

static void check_required_files(uchar *battery_status,
                                 uchar *battery_capacity,
                                 uchar *power_control,
                                 uchar *step_charge,
                                 uchar *current_change,
                                 int *temp_sensor_num,
                                 char **temp_sensor,
                                 char ***current_max_file,
                                 int *current_max_file_num,
                                 char ***current_limit_file,
                                 int *current_limit_file_num,
                                 char ***temp_file,
                                 int *temp_file_num)
{
    if (!file_exists("/sys/class/power_supply/battery/status")) {
        *battery_status = 0;
    }

    if (!file_exists("/sys/class/power_supply/battery/capacity")) {
        *battery_capacity = 0;
    }

    if (!*battery_status || !*battery_capacity) {
        *power_control = 0;

        if (!*battery_status && !*battery_capacity)
            printf_with_time("找不到 battery/status 和 battery/capacity，电量控制失效，伪旁路无法根据充电状态自动启停");
        else if (!*battery_status)
            printf_with_time("找不到 battery/status，伪旁路无法根据充电状态自动启停");
        else
            printf_with_time("找不到 battery/capacity，电量控制失效");
    }

    if (!file_exists("/sys/class/power_supply/battery/charging_enabled") &&
        !file_exists("/sys/class/power_supply/battery/battery_charging_enabled") &&
        !file_exists("/sys/class/power_supply/battery/input_suspend") &&
        !file_exists("/sys/class/qcom-battery/restricted_charging") &&
        !file_exists("/sys/class/qcom-battery/restrict_chg")) {
        *power_control = 0;
        printf_with_time("找不到暂停充电控制文件，电量控制功能失效");
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
    int temp_cap = 16;

    *current_max_file = calloc(max_cap, sizeof(char *));
    *current_limit_file = calloc(limit_cap, sizeof(char *));
    *temp_file = calloc(temp_cap, sizeof(char *));

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
                if (*current_max_file_num >= max_cap) {
                    max_cap *= 2;
                    *current_max_file = realloc(*current_max_file, sizeof(char *) * max_cap);
                }

                (*current_max_file)[*current_max_file_num] = strdup(path);
                (*current_max_file_num)++;
            } else if (ends_with(path, "/thermal_input_current_limit") ||
                       ends_with(path, "/input_current_limit") ||
                       ends_with(path, "/input_current_max")) {
                if (*current_limit_file_num >= limit_cap) {
                    limit_cap *= 2;
                    *current_limit_file = realloc(*current_limit_file, sizeof(char *) * limit_cap);
                }

                (*current_limit_file)[*current_limit_file_num] = strdup(path);
                (*current_limit_file_num)++;
            } else if (ends_with(path, "/temp")) {
                if (*temp_file_num >= temp_cap) {
                    temp_cap *= 2;
                    *temp_file = realloc(*temp_file, sizeof(char *) * temp_cap);
                }

                (*temp_file)[*temp_file_num] = strdup(path);
                (*temp_file_num)++;
            }
        }

        free_string_array(&files, file_num);
    }

    free_string_array(&power_supply_dirs, power_supply_num);

    if (*current_max_file_num == 0) {
        *current_change = 0;
        printf_with_time("未找到充电电流控制文件，电流控制、温控限流、伪旁路供电均失效");
    }

    if (*current_change) {
        *temp_sensor_num = find_temp_sensor(temp_sensor);

        if (*temp_sensor_num == 0) {
            printf_with_time("找不到支持的温度传感器，温度控制功能失效");
        }
    }

    if (!*step_charge && !*power_control && !*current_change) {
        printf_with_time("所有所需文件均不存在，设备不适配，程序退出");
        exit(1000);
    }

    for (int i = 0; i < *current_max_file_num; i++) {
        printf_with_time("找到电流控制文件：%s", (*current_max_file)[i]);
    }

    for (int i = 0; i < *current_limit_file_num; i++) {
        printf_with_time("找到电流限制文件：%s", (*current_limit_file)[i]);
    }

    for (int i = 0; i < *temp_file_num; i++) {
        printf_with_time("找到电池温度文件：%s", (*temp_file)[i]);
    }
}

// 温控移除：挂载相关
#define THERMAL_MOUNT_MAX 512
#define THERMAL_FILES_DIR "/data/adb/modules/turbo-charge/thermal_files"

static char *mounted_thermal_paths[THERMAL_MOUNT_MAX] = {0};
static int mounted_thermal_count = 0;

// 递归列出目录下的所有文件
static int list_dir_recursive(const char *path, char ***out)
{
    if (!path || !out) return 0;

    *out = NULL;
    int count = 0;
    int cap = 64;
    char **list = calloc(cap, sizeof(char *));
    if (!list) return 0;

    // 使用 find 命令递归列出文件
    char cmd[PATH_MAX + 32] = {0};
    snprintf(cmd, sizeof(cmd), "find '%s' -type f", path);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        free(list);
        return 0;
    }

    char line[PATH_MAX];
    while (fgets(line, sizeof(line), fp)) {
        // 去除换行符
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        if (count >= cap) {
            cap *= 2;
            char **tmp = realloc(list, sizeof(char *) * cap);
            if (!tmp) break;
            list = tmp;
        }

        list[count] = strdup(line);
        if (!list[count]) break;
        count++;
    }

    pclose(fp);

    if (count == 0) {
        free(list);
        list = NULL;
    } else {
        char **tmp = realloc(list, sizeof(char *) * count);
        if (tmp) list = tmp;
    }

    *out = list;
    return count;
}

static void mount_thermal_files(void)
{
    printf_with_time("开始挂载温控空文件");

    char thermal_dir[PATH_MAX] = {0};
    snprintf(thermal_dir, sizeof(thermal_dir), "%s", MODDIR_PATH "/thermal_files");

    if (!file_exists(thermal_dir)) {
        printf_with_time("温控目录不存在：%s，跳过温控移除", thermal_dir);
        return;
    }

    // 创建一个空文件作为挂载源
    char empty_file[PATH_MAX] = {0};
    snprintf(empty_file, sizeof(empty_file), "%s/.empty", STATE_DIR);
    ensure_dir(STATE_DIR);

    int fd = open(empty_file, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) close(fd);

    if (!file_exists(empty_file)) {
        printf_with_time("无法创建空文件：%s，跳过温控移除", empty_file);
        return;
    }

    // 递归列出 thermal_files 目录下的所有文件
    char **files = NULL;
    int file_count = list_dir_recursive(thermal_dir, &files);

    if (file_count == 0) {
        printf_with_time("thermal_files 目录为空，跳过温控移除");
        return;
    }

    printf_with_time("找到 %d 个温控空文件", file_count);

    int mounted = 0;
    int skipped = 0;
    int failed = 0;

    for (int i = 0; i < file_count; i++) {
        if (mounted_thermal_count >= THERMAL_MOUNT_MAX) {
            printf_with_time("挂载数量达到上限 %d，停止挂载", THERMAL_MOUNT_MAX);
            break;
        }

        // 计算对应的系统路径
        // thermal_files/system/vendor/etc/xxx -> /system/vendor/etc/xxx
        char sys_path[PATH_MAX] = {0};
        const char *relative = files[i] + strlen(thermal_dir);
        snprintf(sys_path, sizeof(sys_path), "%s", relative);

        // 检查系统文件是否存在
        if (!file_exists(sys_path)) {
            // 系统文件不存在，跳过
            skipped++;
            continue;
        }

        // 执行 bind mount
        if (bind_mount_file(empty_file, sys_path)) {
            mounted_thermal_paths[mounted_thermal_count] = strdup(sys_path);
            mounted_thermal_count++;
            mounted++;
        } else {
            printf_with_time("温控挂载失败：%s", sys_path);
            failed++;
        }
    }

    // 清理
    free_string_array(&files, file_count);

    printf_with_time("温控挂载完成：成功 %d，跳过 %d，失败 %d", mounted, skipped, failed);

    if (mounted > 0) {
        printf_with_time("温控移除功能已生效");
    }
}

static void umount_thermal_files(void)
{
    if (mounted_thermal_count == 0) {
        return;
    }

    printf_with_time("开始卸载温控挂载");

    int unmounted = 0;

    for (int i = 0; i < mounted_thermal_count; i++) {
        if (mounted_thermal_paths[i]) {
            if (unbind_mount_file(mounted_thermal_paths[i])) {
                unmounted++;
            }
            free(mounted_thermal_paths[i]);
            mounted_thermal_paths[i] = NULL;
        }
    }

    mounted_thermal_count = 0;

    // 删除空文件
    char empty_file[PATH_MAX] = {0};
    snprintf(empty_file, sizeof(empty_file), "%s/.empty", STATE_DIR);
    if (file_exists(empty_file)) {
        unlink(empty_file);
    }

    printf_with_time("温控挂载卸载完成：卸载 %d 个", unmounted);
}

static void apply_step_charge_policy(uchar step_charge, const char *power)
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

int main(void)
{
    char *temp_sensor = NULL;
    char **current_limit_file = NULL;
    char **current_max_file = NULL;
    char **temp_file = NULL;

    char charge[32] = {0};
    char power[16] = {0};
    char current_max_char[32] = {0};
    char highest_temp_current_char[32] = {0};
    char thermal[32] = {0};
    char last_appname[APP_PACKAGE_NAME_MAX_SIZE] = {0};

    uchar step_charge = 1;
    uchar power_control = 1;
    uchar current_change = 1;
    uchar battery_status = 1;
    uchar battery_capacity = 1;

    int temp_sensor_num = 0;
    int current_limit_file_num = 0;
    int current_max_file_num = 0;
    int temp_file_num = 0;

    int last_charge_stop = -1;
    int charge_is_stop = 0;
    int is_first_time = 1;
    int is_bypass = 0;
    int screen_is_off = 0;
    int last_charge_status = 0;
    int last_temp_max = -1;

    unsigned long last_option_generation = (unsigned long)-1;

    TempSimState temp_sim_state;
    memset(&temp_sim_state, 0, sizeof(temp_sim_state));
    temp_sim_state.last_enable = -1;
    temp_sim_state.last_value = -1;
    temp_sim_state.last_ok = -1;

    fflush(stdout);

    ensure_dir(STATE_DIR);

    pthread_t option_thread;
    pthread_create(&option_thread, NULL, read_option_file_thread, NULL);
    pthread_detach(option_thread);

    sleep(1);

    check_required_files(&battery_status,
                         &battery_capacity,
                         &power_control,
                         &step_charge,
                         &current_change,
                         &temp_sensor_num,
                         &temp_sensor,
                         &current_max_file,
                         &current_max_file_num,
                         &current_limit_file,
                         &current_limit_file_num,
                         &temp_file,
                         &temp_file_num);

    // 挂载温控空文件
    mount_thermal_files();

    int android_version = 0;

    if (current_change) {
        android_version = check_android_version();
    }

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

    while (1) {
        int cycle_time = read_one_option("CYCLE_TIME");

        snprintf(current_max_char, sizeof(current_max_char), "%d", read_one_option("CURRENT_MAX"));
        snprintf(highest_temp_current_char, sizeof(highest_temp_current_char), "%d", read_one_option("HIGHEST_TEMP_CURRENT"));

        handle_option_generation_change(&last_option_generation, &temp_sim_state);

        if (battery_capacity) {
            read_file("/sys/class/power_supply/battery/capacity", power, sizeof(power));
        } else {
            strcpy(power, "0");
        }

        apply_step_charge_policy(step_charge, power);

        if (!battery_status) {
            if (current_change && android_version > 0) {
                bypass_charge_ctl(android_version,
                                  last_appname,
                                  &is_bypass,
                                  &screen_is_off,
                                  current_max_file,
                                  current_max_file_num);

                if (is_bypass && !screen_is_off) {
                    sleep(cycle_time);
                    continue;
                }
            }

            if (current_change)
                set_array_value(current_max_file, current_max_file_num, current_max_char);

            sleep(cycle_time);
            continue;
        }

        if (!read_file("/sys/class/power_supply/battery/status", charge, sizeof(charge))) {
            sleep(cycle_time);
            continue;
        }

        if (strcmp(charge, "Discharging") != 0) {
            if (is_first_time || !last_charge_status) {
                printf_with_time("充电器已连接");
                last_charge_status = 1;
                is_first_time = 0;
            }

            if (power_control) {
                power_ctl(&last_charge_stop, &charge_is_stop);
            }

            if (current_change && android_version > 0) {
                bypass_charge_ctl(android_version,
                                  last_appname,
                                  &is_bypass,
                                  &screen_is_off,
                                  current_max_file,
                                  current_max_file_num);

                if (is_bypass && !screen_is_off) {
                    sleep(cycle_time);
                    continue;
                }
            }

            if (read_one_option("TEMP_CTRL") == 1 &&
                temp_sensor_num == 1 &&
                current_change &&
                temp_sensor != NULL) {
                if (!read_file(temp_sensor, thermal, sizeof(thermal))) {
                    sleep(cycle_time);
                    continue;
                }

                int temp_int = atoi(thermal);
                int simulated_temp_mc = current_simulated_temp_mc();

                if (simulated_temp_mc > 0) {
                    temp_int = simulated_temp_mc;
                }

                if (temp_int >= read_one_option("TEMP_MAX") * 1000) {
                    printf_with_time("手机温度大于等于限流阈值，限制充电电流为 %s μA",
                                     highest_temp_current_char);

                    while (!is_bypass) {
                        cycle_time = read_one_option("CYCLE_TIME");

                        snprintf(current_max_char, sizeof(current_max_char), "%d", read_one_option("CURRENT_MAX"));
                        snprintf(highest_temp_current_char, sizeof(highest_temp_current_char), "%d", read_one_option("HIGHEST_TEMP_CURRENT"));

                        handle_option_generation_change(&last_option_generation, &temp_sim_state);

                        set_array_value(current_limit_file, current_limit_file_num, "-1");

                        if (!read_file(temp_sensor, thermal, sizeof(thermal))) {
                            break;
                        }

                        temp_int = atoi(thermal);

                        simulated_temp_mc = current_simulated_temp_mc();

                        if (simulated_temp_mc > 0) {
                            temp_int = simulated_temp_mc;
                        }

                        if (last_temp_max == -1)
                            last_temp_max = read_one_option("TEMP_MAX");

                        if (last_temp_max != read_one_option("TEMP_MAX")) {
                            last_temp_max = read_one_option("TEMP_MAX");

                            if (temp_int < read_one_option("TEMP_MAX") * 1000) {
                                printf_with_time("新的温度阈值高于当前温度，恢复充电电流为 %s μA",
                                                 current_max_char);
                                break;
                            } else {
                                printf_with_time("新的温度阈值仍低于当前温度，继续限制充电电流为 %s μA",
                                                 highest_temp_current_char);
                            }
                        }

                        if (!read_file("/sys/class/power_supply/battery/status", charge, sizeof(charge))) {
                            break;
                        }

                        if (strcmp(charge, "Discharging") == 0) {
                            printf_with_time("充电器断开连接，恢复充电电流为 %s μA", current_max_char);
                            last_charge_status = 0;
                            break;
                        }

                        if (temp_int <= read_one_option("RECHARGE_TEMP") * 1000) {
                            printf_with_time("手机温度小于等于恢复快充阈值，恢复充电电流为 %s μA",
                                             current_max_char);
                            break;
                        }

                        if (read_one_option("TEMP_CTRL") != 1) {
                            printf_with_time("温控关闭，恢复充电电流为 %s μA", current_max_char);
                            break;
                        }

                        if (battery_capacity) {
                            read_file("/sys/class/power_supply/battery/capacity", power, sizeof(power));
                        }

                        apply_step_charge_policy(step_charge, power);

                        set_array_value(current_max_file, current_max_file_num, highest_temp_current_char);

                        if (power_control) {
                            power_ctl(&last_charge_stop, &charge_is_stop);
                        }

                        if (current_change && android_version > 0) {
                            bypass_charge_ctl(android_version,
                                              last_appname,
                                              &is_bypass,
                                              &screen_is_off,
                                              current_max_file,
                                              current_max_file_num);

                            if (is_bypass && !screen_is_off)
                                break;
                        }

                        sleep(cycle_time);
                    }
                }
            }

            if (current_change && !is_bypass) {
                set_array_value(current_max_file, current_max_file_num, current_max_char);
            }
        } else {
            if (is_first_time) {
                printf_with_time("充电器未连接");
                is_first_time = 0;
                last_charge_status = 0;
            } else if (last_charge_status) {
                printf_with_time("充电器断开连接");
                last_charge_status = 0;
            }

            if (is_bypass) {
                printf_with_time("手机未在充电状态，伪旁路供电功能暂时停用");
                is_bypass = 0;
            }

            stop_foreground_thread();

            if (power_control) {
                power_ctl(&last_charge_stop, &charge_is_stop);
            }
        }

        sleep(cycle_time);
    }

    // 程序退出时卸载温控挂载
    umount_thermal_files();

    return 0;
}
