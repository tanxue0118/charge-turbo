#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>
#include <errno.h>
#include <ctype.h>
#include <sys/inotify.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <signal.h>

#define BYPASS_CHARGE_CURRENT "500000"
#define APP_PACKAGE_NAME_MAX_SIZE 100
#define OPTION_NAME_MAX_SIZE 64
#define LOG_BUF_SIZE 1024
#define FOREGROUND_POLL_SECONDS 10

#define MODDIR_PATH "/data/adb/modules/turbo-charge"
#define STATE_DIR MODDIR_PATH "/state"
#define TEMP_NODE_MAX 64
#define MEIZU_WIRED_LEVEL_PATH "/sys/class/meizu/charger/wired/wired_level"
#define MEIZU_WIRED_LEVEL_LEGACY_PATH "/sys/class/meizu/charger/wired_level"
#define MEIZU_WIRED_LEVEL_PATHS_TEXT MEIZU_WIRED_LEVEL_PATH "," MEIZU_WIRED_LEVEL_LEGACY_PATH
#define MEIZU_THERMAL_FLYME_CLEAR_DIR MODDIR_PATH "/meizu_files/thermal_flyme_clear"
#define MEIZU_THERMAL_EXTREMEGT_DIR MODDIR_PATH "/meizu_files/thermal_extremegt"
#define MEIZU_THERMAL_SCHEME_FLYME_CLEAR 1
#define MEIZU_THERMAL_SCHEME_EXTREMEGT 2
#define MEIZU_LEVEL_INACTIVE -1
#define MEIZU_LEVEL_NODE_MISSING -2
#define MEIZU_LEVEL_WRITE_FAILED -3

typedef unsigned char uchar;

typedef struct {
    const char *name;
    int value;
    int default_value;
} Option;

typedef struct {
    char target[PATH_MAX];
    char fake[PATH_MAX];
    char label[128];
    int unit;
    int mounted;
} TempFakeNode;

typedef struct {
    TempFakeNode nodes[TEMP_NODE_MAX];
    int count;
    int discovered;
    int last_value;
    int last_simulating;
} TempSimState;

typedef struct {
    int mounted;
    int charging;
    int last_mode;
} MountModeState;

typedef enum {
    BYPASS_MODE_OFF = 0,
    BYPASS_MODE_HARDWARE,
    BYPASS_MODE_COMPATIBILITY,
    BYPASS_MODE_UNAVAILABLE
} BypassMode;

typedef struct {
    BypassMode mode;
    char node_path[PATH_MAX];
    char restore_value[128];
    char active_value[128];
    int restore_warning_logged;
    time_t next_probe_time;
} BypassState;

#define BYPASS_STATE_INITIALIZER \
    { BYPASS_MODE_OFF, {0}, {0}, {0}, 0, 0 }

typedef struct {
    int last_charge_stop;
    int last_mode;
    int active;
    int stop_applied;
    int stop_unsupported_logged;
    int threshold_warning_logged;
} PowerControlState;

#define POWER_CONTROL_STATE_INITIALIZER \
    { -1, -1, 0, 0, 0, 0 }

static const char option_dir[] = MODDIR_PATH;
static const char option_name[] = "option.txt";
static const char option_file[] = MODDIR_PATH "/option.txt";
static const char bypass_charge_file[] = MODDIR_PATH "/bypass_charge.txt";

static void mount_thermal_files(void);
static void umount_thermal_files(void);
static void sync_thermal_mount_mode(int is_charging, MountModeState *state);
static int is_bypass_active(const BypassState *state);
static int clamp_meizu_charge_level(int level);
static const char *meizu_thermal_scheme_name(int scheme);

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
    {"TEMP_SIMULATE", 0, 0},
    {"TEMP_SIMULATE_MOUNT_MODE", 0, 0},
    {"TEMP_SIMULATE_VALUE", 28, 28},
    {"THERMAL_MOUNT_MODE", 0, 0},
    {"BYPASS_CHARGE", 0, 0},
    {"TEMP_LEVEL1", 45, 45},
    {"TEMP_LEVEL1_CURRENT", 3000000, 3000000},
    {"TEMP_LEVEL2", 50, 50},
    {"TEMP_LEVEL2_CURRENT", 1000000, 1000000},
    {"POWER_CTRL_MODE", 0, 0},
    {"MEIZU_DEVICE", 0, 0},
    {"MEIZU_CHARGE_LEVEL", 10, 10},
    {"MEIZU_THERMAL_SCHEME", 2, 2}
};

static const int option_count = sizeof(options) / sizeof(options[0]);

static pthread_mutex_t mutex_options = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_foreground_app = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_thread = PTHREAD_MUTEX_INITIALIZER;

static unsigned long option_generation = 0;
static volatile sig_atomic_t program_running = 1;

static char foreground_app_name[APP_PACKAGE_NAME_MAX_SIZE] = {0};
static int foreground_thread_running = 0;
static int foreground_thread_stop = 0;

static void printf_with_time(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void handle_exit_signal(int sig)
{
    (void)sig;
    program_running = 0;
}

static void log_foreground_error_once(int *last_error, int code, const char *msg)
{
    if (!last_error || !msg) return;

    if (*last_error != code) {
        printf_with_time("%s", msg);
        *last_error = code;
    }
}

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

    struct stat statbuf;
    if (stat(file, &statbuf) != 0) return;

    FILE *fp = fopen(file, "r+");

    if (!fp) {
        chmod(file, 0644);
        fp = fopen(file, "r+");
    }

    if (!fp) return;

    char *old = calloc(1, statbuf.st_size + 1);
    if (!old) { fclose(fp); return; }

    if (fgets(old, statbuf.st_size + 1, fp)) {
        line_feed(old);
    }

    if (strcmp(old, value) != 0) {
        rewind(fp);
        fputs(value, fp);
        fflush(fp);
    }

    free(old);
    fclose(fp);
}

static void set_array_value(char **files, int num, const char *value)
{
    if (!files || num <= 0 || !value) return;

    for (int i = 0; i < num; i++) {
        if (files[i]) set_value(files[i], value);
    }
}

static int run_shell_command(const char *cmd)
{
    if (!cmd || !cmd[0]) return -1;

    int status = system(cmd);
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);

    return status;
}

static int write_meizu_wired_level_node_with_echo(const char *path, int level)
{
    if (!path) return -1;

    char cmd[PATH_MAX * 3 + 96] = {0};
    snprintf(cmd, sizeof(cmd), "chmod 777 %s 2>/dev/null", path);
    int ret = run_shell_command(cmd);
    if (ret != 0) {
        printf_with_time("Meizu wired_level chmod-before-write failed, path=%s, level=%d, ret=%d",
                         path, level, ret);
        return ret;
    }

    snprintf(cmd, sizeof(cmd), "echo %d > %s", level, path);
    ret = run_shell_command(cmd);
    if (ret != 0) {
        printf_with_time("Meizu wired_level echo failed, path=%s, level=%d, ret=%d",
                         path, level, ret);
        return ret;
    }

    snprintf(cmd, sizeof(cmd), "chmod -w %s 2>/dev/null", path);
    ret = run_shell_command(cmd);
    if (ret != 0) {
        printf_with_time("Meizu wired_level chmod-lock failed, path=%s, level=%d, ret=%d",
                         path, level, ret);
        return ret;
    }

    return 0;
}

static int restore_meizu_wired_level_node_permission(const char *path)
{
    if (!path) return -1;

    char cmd[PATH_MAX + 32] = {0};
    snprintf(cmd, sizeof(cmd), "chmod 777 %s 2>/dev/null", path);

    return run_shell_command(cmd);
}

static const char *meizu_wired_level_paths[] = {
    MEIZU_WIRED_LEVEL_PATH,
    MEIZU_WIRED_LEVEL_LEGACY_PATH
};

static int write_meizu_wired_level_with_echo(int level,
                                             int *found_count,
                                             int *success_count)
{
    level = clamp_meizu_charge_level(level);

    int found = 0;
    int success = 0;
    int first_error = 0;
    int path_count = sizeof(meizu_wired_level_paths) / sizeof(meizu_wired_level_paths[0]);

    for (int i = 0; i < path_count; i++) {
        const char *path = meizu_wired_level_paths[i];
        if (!file_exists(path)) continue;

        found++;
        printf_with_time("Meizu wired_level node found, path=%s, level=%d, method=chmod777-echo-chmod-w",
                         path, level);
        int ret = write_meizu_wired_level_node_with_echo(path, level);
        if (ret == 0) {
            success++;
            printf_with_time("Meizu wired_level write success, path=%s, level=%d", path, level);
        } else {
            if (first_error == 0) first_error = ret;
            printf_with_time("Meizu wired_level write failed, path=%s, level=%d, ret=%d",
                             path, level, ret);
        }
    }

    if (found == 0) {
        printf_with_time("Meizu wired_level nodes not found, paths=%s", MEIZU_WIRED_LEVEL_PATHS_TEXT);
    } else {
        printf_with_time("Meizu wired_level write summary, level=%d, success=%d, found=%d",
                         level, success, found);
    }

    if (found_count) *found_count = found;
    if (success_count) *success_count = success;

    if (found == 0) return MEIZU_LEVEL_NODE_MISSING;
    if (success == 0) return first_error ? first_error : MEIZU_LEVEL_WRITE_FAILED;

    return 0;
}

static int restore_meizu_wired_level_permission(void)
{
    int found = 0;
    int failed = 0;
    int path_count = sizeof(meizu_wired_level_paths) / sizeof(meizu_wired_level_paths[0]);

    for (int i = 0; i < path_count; i++) {
        const char *path = meizu_wired_level_paths[i];
        if (!file_exists(path)) continue;

        found++;
        int ret = restore_meizu_wired_level_node_permission(path);
        if (ret != 0) {
            failed++;
            printf_with_time("Meizu wired_level chmod 777 failed, path=%s, ret=%d", path, ret);
        } else {
            printf_with_time("Meizu wired_level chmod 777 success, path=%s", path);
        }
    }

    if (found == 0) {
        printf_with_time("Meizu wired_level chmod 777 skipped, nodes missing, paths=%s",
                         MEIZU_WIRED_LEVEL_PATHS_TEXT);
        return MEIZU_LEVEL_NODE_MISSING;
    }
    if (failed > 0) return -1;

    return 0;
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

static int clamp_meizu_charge_level(int level)
{
    if (level < 1) return 10;
    if (level > 10) return 10;
    return level;
}

static int clamp_meizu_thermal_scheme(int scheme)
{
    if (scheme == MEIZU_THERMAL_SCHEME_FLYME_CLEAR) return scheme;
    if (scheme == MEIZU_THERMAL_SCHEME_EXTREMEGT) return scheme;
    return MEIZU_THERMAL_SCHEME_EXTREMEGT;
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

        if (strcmp(options[idx].name, "MEIZU_CHARGE_LEVEL") == 0) {
            int clamped = clamp_meizu_charge_level(new_value);
            if (clamped != new_value) {
                if (first_run)
                    printf_with_time("MEIZU_CHARGE_LEVEL 的值 %d 不在 1-10 范围内，使用默认值 %d",
                                     new_value,
                                     clamped);
                else
                    printf_with_time("MEIZU_CHARGE_LEVEL 的值 %d 不在 1-10 范围内，改为 %d",
                                     new_value,
                                     clamped);
                new_value = clamped;
            }
        }

        if (strcmp(options[idx].name, "MEIZU_THERMAL_SCHEME") == 0) {
            int clamped = clamp_meizu_thermal_scheme(new_value);
            if (clamped != new_value) {
                if (first_run)
                    printf_with_time("MEIZU_THERMAL_SCHEME 的值 %d 不在 1-2 范围内，使用默认值 %d",
                                     new_value,
                                     clamped);
                else
                    printf_with_time("MEIZU_THERMAL_SCHEME 的值 %d 不在 1-2 范围内，改为 %d",
                                     new_value,
                                     clamped);
                new_value = clamped;
            }
        }

        if (new_value != 0 && new_value != 1) {
            if (strcmp(options[idx].name, "TEMP_SIMULATE_MOUNT_MODE") == 0 ||
                strcmp(options[idx].name, "THERMAL_MOUNT_MODE") == 0) {
                new_value = new_value ? 1 : 0;
            }
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

static int read_temp_mc(const char *path, int *out)
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
            if (unbind_mount_file(n->target)) {
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

static int cleanup_battery_temp_simulation(TempSimState *st)
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

static int apply_battery_temp_simulation(TempSimState *st, int is_charging)
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

    st->last_value = value;
    st->last_simulating = 1;

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

static void handle_meizu_generation_change(int *last_meizu_thermal_key,
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

static void restore_meizu_wired_level(int *last_locked_level)
{
    if (!last_locked_level) return;

    if (*last_locked_level >= 1) {
        printf_with_time("准备恢复魅族充电档位节点写权限，当前锁定档位=%d", *last_locked_level);
    }

    if (*last_locked_level >= 1 || *last_locked_level == MEIZU_LEVEL_WRITE_FAILED) {
        int ret = restore_meizu_wired_level_permission();
        if (ret != 0) {
            printf_with_time("恢复魅族充电档位节点写权限失败，路径=%s，返回码=%d",
                             MEIZU_WIRED_LEVEL_PATHS_TEXT, ret);
            return;
        }
        printf_with_time("魅族充电档位节点已恢复写权限，路径=%s", MEIZU_WIRED_LEVEL_PATHS_TEXT);
    }

    *last_locked_level = MEIZU_LEVEL_INACTIVE;
}

static void sync_meizu_wired_level(int is_charging, int *last_locked_level)
{
    if (!last_locked_level) return;

    if (read_one_option("MEIZU_DEVICE") != 1 || !is_charging) {
        if (read_one_option("MEIZU_DEVICE") != 1) {
            printf_with_time("魅族适配未开启，跳过档位写入");
        } else {
            printf_with_time("当前未在充电，恢复魅族档位节点写权限");
        }
        restore_meizu_wired_level(last_locked_level);
        return;
    }

    int level = clamp_meizu_charge_level(read_one_option("MEIZU_CHARGE_LEVEL"));
    printf_with_time("准备写入魅族充电档位，原始值=%d，修正后=%d，候选节点=%s",
                     read_one_option("MEIZU_CHARGE_LEVEL"), level, MEIZU_WIRED_LEVEL_PATHS_TEXT);
    int found_nodes = 0;
    int success_nodes = 0;
    int ret = write_meizu_wired_level_with_echo(level, &found_nodes, &success_nodes);

    if (ret == MEIZU_LEVEL_NODE_MISSING) {
        if (*last_locked_level != MEIZU_LEVEL_NODE_MISSING) {
            printf_with_time("Meizu wired_level nodes missing, paths=%s,%s",
                             MEIZU_WIRED_LEVEL_PATH, MEIZU_WIRED_LEVEL_LEGACY_PATH);
        }
        *last_locked_level = MEIZU_LEVEL_NODE_MISSING;
        return;
    }

    if (ret != 0) {
        if (*last_locked_level != MEIZU_LEVEL_WRITE_FAILED) {
            printf_with_time("魅族充电档位写入失败，档位=%d，候选节点=%s，返回码=%d，成功节点=%d/%d",
                             level, MEIZU_WIRED_LEVEL_PATHS_TEXT, ret, success_nodes, found_nodes);
        }
        *last_locked_level = MEIZU_LEVEL_WRITE_FAILED;
        return;
    }

    if (*last_locked_level != level) {
        printf_with_time("Meizu wired_level write complete, level=%d, success=%d, found=%d",
                         level, success_nodes, found_nodes);
        printf_with_time("魅族充电档位写入成功，已锁定为 %d，成功节点=%d/%d，候选节点=%s",
                         level, success_nodes, found_nodes, MEIZU_WIRED_LEVEL_PATHS_TEXT);
    }

    *last_locked_level = level;
}

static int check_android_version(void)
{
    char version[32] = {0};
    FILE *fp = popen("getprop ro.build.version.release", "r");

    if (!fp) {
        printf_with_time("无法获取安卓版本，应用旁路供电功能失效");
        return 0;
    }

    if (!fgets(version, sizeof(version), fp)) {
        pclose(fp);
        printf_with_time("无法读取安卓版本，应用旁路供电功能失效");
        return 0;
    }

    pclose(fp);
    line_feed(version);

    int android_version = atoi(version);

    if (android_version < 7) {
        printf_with_time("安卓版本低于 7，无法可靠获取前台应用，应用旁路供电功能失效");
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

static int load_bypass_app_list(char ***apps, int *app_num, time_t *last_mtime);

static char *fast_append_u32(char *p, char *end, uint32_t v)
{
    char tmp[10];
    char *out = tmp + sizeof(tmp);

    if (!p || !end || p >= end) return p;

    do {
        *--out = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v);

    while (out < tmp + sizeof(tmp) && p < end) {
        *p++ = *out++;
    }

    return p;
}

static int fast_parse_u32(const char *s, uint32_t *out)
{
    uint32_t v = 0;

    if (!s || !*s || !out) return 0;

    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        uint32_t d;

        if (c < '0' || c > '9') return 0;
        d = (uint32_t)(c - '0');

        if (v > (UINT32_MAX - d) / 10u) return 0;
        v = v * 10u + d;
    }

    *out = v;
    return 1;
}

static int fast_proc_path(char *buf, size_t size, int pid, const char *suffix)
{
    static const char prefix[] = "/proc/";
    char *p = buf;
    char *end;
    size_t prefix_len = sizeof(prefix) - 1;
    size_t suffix_len;

    if (!buf || size == 0 || pid < 0 || !suffix) return -1;

    end = buf + size - 1;

    if ((size_t)(end - p) < prefix_len) return -1;
    memcpy(p, prefix, prefix_len);
    p += prefix_len;

    p = fast_append_u32(p, end, (uint32_t)pid);
    suffix_len = strlen(suffix);

    if ((size_t)(end - p) < suffix_len) return -1;
    memcpy(p, suffix, suffix_len);
    p += suffix_len;

    *p = '\0';
    return 0;
}

static const char *cpuset_foreground_paths[] = {
    "/dev/cpuset/top-app/tasks",
    "/dev/cpuset/foreground/tasks",
};

static int read_proc_cmdline(int pid, char *out, size_t out_size)
{
    if (!out || out_size == 0) return 0;
    out[0] = '\0';

    char path[64] = {0};
    if (fast_proc_path(path, sizeof(path), pid, "/cmdline") != 0) return 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;

    ssize_t n = read(fd, out, out_size - 1);
    close(fd);

    if (n <= 0) return 0;

    out[(size_t)n] = '\0';

    for (size_t i = 0; i < (size_t)n; i++) {
        if (out[i] == ':') {
            out[i] = '\0';
            break;
        }
    }

    return out[0] ? 1 : 0;
}

static int read_foreground_from_cpuset(char **bypass_apps, int bypass_app_num,
                                       char *out, size_t out_size)
{
    if (!out || out_size == 0) return -1;
    out[0] = '\0';

    int any_group = 0;
    int group_count = sizeof(cpuset_foreground_paths) / sizeof(cpuset_foreground_paths[0]);

    for (int g = 0; g < group_count; g++) {
        FILE *fp = fopen(cpuset_foreground_paths[g], "r");
        if (!fp) continue;

        any_group = 1;

        char line[16] = {0};
        char cmd[APP_PACKAGE_NAME_MAX_SIZE] = {0};

        while (fgets(line, sizeof(line), fp)) {
            line_feed(line);

            uint32_t pid_u = 0;
            if (!fast_parse_u32(line, &pid_u) || pid_u > 2147483647u) continue;

            if (!read_proc_cmdline((int)pid_u, cmd, sizeof(cmd))) continue;

            for (int i = 0; i < bypass_app_num; i++) {
                if (bypass_apps[i] && strcmp(cmd, bypass_apps[i]) == 0) {
                    snprintf(out, out_size, "%s", cmd);
                    fclose(fp);
                    return 1;
                }
            }

            if (out[0] == '\0' && cmd[0]) {
                snprintf(out, out_size, "%s", cmd);
            }
        }

        fclose(fp);
    }

    if (!any_group) return -1;

    return 0;
}

static void *foreground_thread_func(void *arg)
{
    free(arg);
    int last_error = 0;

    char **bypass_apps = NULL;
    int bypass_app_num = 0;
    time_t bypass_file_last_mtime = 0;

    while (!should_stop_foreground_thread() && read_one_option("BYPASS_CHARGE") == 1) {
        char result[256] = {0};
        FILE *fp = popen("dumpsys deviceidle | grep 'mScreenOn'", "r");

        if (!fp) {
            log_foreground_error_once(&last_error, 1, "无法执行 dumpsys deviceidle");
            sleep(FOREGROUND_POLL_SECONDS);
            continue;
        }

        fgets(result, sizeof(result), fp);
        pclose(fp);
        line_feed(result);

        char *eq = strchr(result, '=');

        if (!eq) {
            log_foreground_error_once(&last_error, 2, "无法获取屏幕状态");
            sleep(FOREGROUND_POLL_SECONDS);
            continue;
        }

        if (strcmp(eq + 1, "true") != 0) {
            set_foreground_app("screen_is_off");
            sleep(FOREGROUND_POLL_SECONDS);
            continue;
        }

        load_bypass_app_list(&bypass_apps, &bypass_app_num, &bypass_file_last_mtime);

        char pkg[APP_PACKAGE_NAME_MAX_SIZE] = {0};
        int r = read_foreground_from_cpuset(bypass_apps, bypass_app_num, pkg, sizeof(pkg));

        if (r < 0) {
            log_foreground_error_once(&last_error, 3, "无法读取 top-app cpuset，前台应用获取失败");
            sleep(FOREGROUND_POLL_SECONDS);
            continue;
        }

        last_error = 0;
        set_foreground_app(pkg);

        sleep(FOREGROUND_POLL_SECONDS);
    }

    set_foreground_app("");
    free_string_array(&bypass_apps, bypass_app_num);

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

    printf_with_time("旁路供电应用列表已重新读取，共 %d 个应用", count);

    return 1;
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

static int read_external_power_state(void)
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

static int is_bypass_active(const BypassState *state)
{
    return state &&
           (state->mode == BYPASS_MODE_HARDWARE ||
            state->mode == BYPASS_MODE_COMPATIBILITY);
}

static void power_ctl(PowerControlState *state,
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

static void bypass_charge_ctl(int android_version,
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

static void sync_bypass_control(BypassState *bypass_state,
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

static void restore_charge_control(BypassState *bypass_state,
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

#define THERMAL_MOUNT_MAX 1024
#define THERMAL_FILES_DIR "/data/adb/modules/turbo-charge/thermal_files"

static char *mounted_thermal_paths[THERMAL_MOUNT_MAX] = {0};
static int mounted_thermal_count = 0;

static int list_dir_recursive(const char *path, char ***out)
{
    if (!path || !out) return 0;

    *out = NULL;
    int count = 0;
    int cap = 64;
    char **list = calloc(cap, sizeof(char *));
    if (!list) return 0;

    char cmd[PATH_MAX + 32] = {0};
    snprintf(cmd, sizeof(cmd), "find '%s' -type f", path);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        free(list);
        return 0;
    }

    char line[PATH_MAX];
    while (fgets(line, sizeof(line), fp)) {
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

static int get_charging_state(const char *status)
{
    if (!status) return 0;

    return strcmp(status, "Charging") == 0 ||
           strcmp(status, "Full") == 0 ||
           strcmp(status, "Not charging") == 0;
}

static void sync_thermal_mount_mode(int is_charging, MountModeState *state)
{
    if (!state) return;

    int mode = read_one_option("THERMAL_MOUNT_MODE");

    if (mode != 0 && mode != 1) mode = 0;

    if (mode == 0) {
        if (!state->mounted) {
            mount_thermal_files();
            state->mounted = mounted_thermal_count > 0;
        }
        state->charging = is_charging;
        state->last_mode = mode;
        return;
    }

    if (is_charging) {
        if (!state->mounted) {
            mount_thermal_files();
            state->mounted = mounted_thermal_count > 0;
        }
    } else if (state->mounted) {
        umount_thermal_files();
        state->mounted = 0;
    }

    state->charging = is_charging;
    state->last_mode = mode;
}

static const char *thermal_mount_source_for_target(const char *target,
                                                   const char *empty_file)
{
    if (!target || !empty_file) return empty_file;

    return empty_file;
}

static int should_mount_thermal_target(const char *target)
{
    return target && target[0];
}

static const char *meizu_thermal_scheme_name(int scheme)
{
    if (scheme == MEIZU_THERMAL_SCHEME_FLYME_CLEAR) return "Flyme clear";
    return "extremegt relaxed";
}

static const char *select_thermal_files_dir(int *meizu_device_out, int *scheme_out)
{
    int meizu_device = read_one_option("MEIZU_DEVICE") == 1 ? 1 : 0;
    int scheme = clamp_meizu_thermal_scheme(read_one_option("MEIZU_THERMAL_SCHEME"));

    if (meizu_device_out) *meizu_device_out = meizu_device;
    if (scheme_out) *scheme_out = scheme;

    if (!meizu_device) return THERMAL_FILES_DIR;
    if (scheme == MEIZU_THERMAL_SCHEME_FLYME_CLEAR) return MEIZU_THERMAL_FLYME_CLEAR_DIR;

    return MEIZU_THERMAL_EXTREMEGT_DIR;
}

static void mount_thermal_files(void)
{
    int meizu_device = 0;
    int meizu_scheme = MEIZU_THERMAL_SCHEME_EXTREMEGT;
    const char *selected_thermal_dir = select_thermal_files_dir(&meizu_device, &meizu_scheme);

    printf_with_time("Start thermal mount, MEIZU_DEVICE=%d, MEIZU_THERMAL_SCHEME=%d(%s), dir=%s",
                     meizu_device,
                     meizu_scheme,
                     meizu_thermal_scheme_name(meizu_scheme),
                     selected_thermal_dir);

    printf_with_time("开始挂载温控文件，MEIZU_DEVICE=%d", read_one_option("MEIZU_DEVICE"));

    char thermal_dir[PATH_MAX] = {0};
    snprintf(thermal_dir, sizeof(thermal_dir), "%s", selected_thermal_dir);

    if (!file_exists(thermal_dir)) {
        printf_with_time("温控目录不存在：%s，跳过温控挂载", thermal_dir);
        return;
    }

    char empty_file[PATH_MAX] = {0};
    snprintf(empty_file, sizeof(empty_file), "%s/.empty", STATE_DIR);
    ensure_dir(STATE_DIR);

    int fd = open(empty_file, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) close(fd);

    if (!file_exists(empty_file)) {
        printf_with_time("无法创建空文件：%s，跳过温控挂载", empty_file);
        return;
    }

    char **files = NULL;
    int file_count = list_dir_recursive(thermal_dir, &files);

    if (file_count == 0) {
        printf_with_time("thermal_files 目录为空，跳过温控挂载");
        return;
    }

    printf_with_time("找到 %d 个温控挂载文件", file_count);
    if (meizu_device) {
        printf_with_time("Meizu thermal scheme active, scheme=%d(%s), dir=%s",
                         meizu_scheme,
                         meizu_thermal_scheme_name(meizu_scheme),
                         selected_thermal_dir);
    }

    int mounted = 0;
    int skipped = 0;
    int failed = 0;
    int meizu_mounted = 0;

    for (int i = 0; i < file_count; i++) {
        if (mounted_thermal_count >= THERMAL_MOUNT_MAX) {
            printf_with_time("挂载数量达到上限 %d，停止挂载", THERMAL_MOUNT_MAX);
            break;
        }

        char sys_path[PATH_MAX] = {0};
        const char *relative = files[i] + strlen(thermal_dir);
        snprintf(sys_path, sizeof(sys_path), "%s", relative);

        if (!should_mount_thermal_target(sys_path)) {
            if (read_one_option("MEIZU_DEVICE") == 1) {
                printf_with_time("魅族模式跳过非目标温控文件：%s", sys_path);
            }
            skipped++;
            continue;
        }

        if (!file_exists(sys_path)) {
            printf_with_time("目标温控文件不存在，跳过：%s", sys_path);
            skipped++;
            continue;
        }

        const char *source_file = meizu_device ? files[i] : thermal_mount_source_for_target(sys_path, empty_file);
        printf_with_time("准备挂载温控文件，目标=%s，源=%s", sys_path, source_file);

        if (bind_mount_file(source_file, sys_path)) {
            mounted_thermal_paths[mounted_thermal_count] = strdup(sys_path);
            mounted_thermal_count++;
            mounted++;
            if (meizu_device)
                meizu_mounted++;
        } else {
            printf_with_time("温控挂载失败，目标=%s，源=%s", sys_path, source_file);
            failed++;
        }
    }

    free_string_array(&files, file_count);

    printf_with_time("温控挂载完成：成功 %d，跳过 %d，失败 %d", mounted, skipped, failed);

    if (mounted > 0) {
        printf_with_time("温控移除功能已生效");
        if (meizu_mounted > 0)
            printf_with_time("Meizu thermal scheme mounted, scheme=%d(%s)",
                             meizu_scheme,
                             meizu_thermal_scheme_name(meizu_scheme));
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
    int last_meizu_locked_level = MEIZU_LEVEL_INACTIVE;

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

        if (battery_capacity) {
            read_file("/sys/class/power_supply/battery/capacity", power, sizeof(power));
        } else {
            strcpy(power, "0");
        }

        charge[0] = '\0';
        if (battery_status) {
            read_file("/sys/class/power_supply/battery/status", charge, sizeof(charge));
        }
        is_charging = get_power_connection_state(battery_status, charge);

        sync_thermal_mount_mode(is_charging, &thermal_mount_state);
        handle_option_generation_change(&last_option_generation, &temp_sim_state, is_charging);
        handle_meizu_generation_change(&last_meizu_device, &thermal_mount_state, is_charging);
        apply_battery_temp_simulation(&temp_sim_state, is_charging);
        sync_meizu_wired_level(is_charging, &last_meizu_locked_level);

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
                    sync_meizu_wired_level(0, &last_meizu_locked_level);
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

                sync_meizu_wired_level(1, &last_meizu_locked_level);

                if (temp_mc >= max_mc) {
                    printf_with_time("温度 >= %d℃（第三档），停止充电", read_one_option("TEMP_MAX"));
                    charge_ctl("0");
                    while (program_running) {
                        sleep(cycle_time);
                        simulated_temp_mc = current_simulated_temp_mc();
                        if (simulated_temp_mc > 0) temp_mc = simulated_temp_mc;
                        else if (!read_temp_mc(temp_sensor, &temp_mc)) break;
                        max_mc = read_one_option("TEMP_MAX") * 1000;
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

                    if (battery_capacity)
                        read_file("/sys/class/power_supply/battery/capacity", power, sizeof(power));

                    apply_step_charge_policy(step_charge, power);

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
    restore_meizu_wired_level(&last_meizu_locked_level);
    umount_thermal_files();

    return 0;
}
