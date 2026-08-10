#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "foreground_app.h"
#include "printf_with_time.h"
#include "read_option_file.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static void log_foreground_error_once(int *last_error, int code, const char *msg)
{
    if (!last_error || !msg) return;

    if (*last_error != code) {
        printf_with_time("%s", msg);
        *last_error = code;
    }
}

int check_android_version(void)
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

void get_foreground_app(char *out, size_t out_size)
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

int load_bypass_app_list(char ***apps, int *app_num, time_t *last_mtime);

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

void start_foreground_thread_if_needed(int android_version)
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

void stop_foreground_thread(void)
{
    pthread_mutex_lock(&mutex_thread);

    if (foreground_thread_running) {
        foreground_thread_stop = 1;
    }

    pthread_mutex_unlock(&mutex_thread);
}

int load_bypass_app_list(char ***apps, int *app_num, time_t *last_mtime)
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
