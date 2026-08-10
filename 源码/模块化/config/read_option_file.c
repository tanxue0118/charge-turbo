#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "printf_with_time.h"
#include "read_option_file.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>
static int option_index(const char *name)
{
    for (int i = 0; i < option_count; i++) {
        if (strcmp(options[i].name, name) == 0) return i;
    }

    return -1;
}

int read_one_option(const char *name)
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

int read_bool_option(const char *name)
{
    return read_one_option(name) == 1 ? 1 : 0;
}

unsigned long read_option_generation(void)
{
    pthread_mutex_lock(&mutex_options);
    unsigned long gen = option_generation;
    pthread_mutex_unlock(&mutex_options);

    return gen;
}

/* 首次读取和热重载的提示语不同，但参数完全一致，这里按 first_run 选择格式串。 */
static void log_option_message(int first_run,
                               const char *first_run_format,
                               const char *reload_format,
                               ...)
{
    va_list ap;

    va_start(ap, reload_format);
    vprintf_with_time(first_run ? first_run_format : reload_format, ap);
    va_end(ap);
}

static void clamp_option_value(int first_run,
                               const char *name,
                               int *value,
                               int (*clamp)(int),
                               const char *range_text)
{
    int clamped = clamp(*value);

    if (clamped == *value) return;

    log_option_message(first_run,
                       "%s 的值 %d 不在 %s 范围内，使用默认值 %d",
                       "%s 的值 %d 不在 %s 范围内，改为 %d",
                       name, *value, range_text, clamped);

    *value = clamped;
}

static void load_option_file(int first_run)
{
    FILE *fp = fopen(option_file, "r");

    if (!fp) {
        log_option_message(first_run,
                           "无法打开配置文件 %s，使用默认配置",
                           "无法打开配置文件 %s，沿用上一次配置",
                           option_file);
        return;
    }

    int found[option_count];
    memset(found, 0, sizeof(found));

    char line[256];

    pthread_mutex_lock(&mutex_options);

    while (fgets(line, sizeof(line), fp)) {
        char *p = trim_config_line(line);
        if (!p) continue;

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
            log_option_message(first_run,
                               "配置文件 %s 的值为空、非纯数字或超过范围，使用默认值 %d",
                               "%s 的值非法，沿用上一次的值 %d",
                               options[idx].name, options[idx].value);
            continue;
        }

        if (strcmp(options[idx].name, "CYCLE_TIME") == 0 && new_value == 0) {
            log_option_message(first_run,
                               "CYCLE_TIME 的值为 0，这是不允许的，使用默认值 %d",
                               "CYCLE_TIME 的值为 0，这是不允许的，沿用上一次的值 %d",
                               options[idx].value);
            continue;
        }

        if (strcmp(options[idx].name, "MEIZU_CHARGE_LEVEL") == 0) {
            clamp_option_value(first_run, options[idx].name, &new_value,
                               clamp_meizu_charge_level, "1-10");
        }

        if (strcmp(options[idx].name, "MEIZU_THERMAL_SCHEME") == 0) {
            clamp_option_value(first_run, options[idx].name, &new_value,
                               clamp_meizu_thermal_scheme, "1-2");
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
            log_option_message(first_run,
                               "配置文件中不存在 %s，使用默认值 %d",
                               "配置文件中不存在 %s，沿用上一次的值 %d",
                               options[i].name, options[i].value);
        }
    }

    option_generation++;

    pthread_mutex_unlock(&mutex_options);

    fclose(fp);
}

static int watch_option_dir(int fd)
{
    return inotify_add_watch(fd,
                             option_dir,
                             IN_CLOSE_WRITE |
                             IN_MOVED_TO |
                             IN_CREATE |
                             IN_ATTRIB |
                             IN_DELETE_SELF |
                             IN_MOVE_SELF);
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

void *read_option_file_thread(void *arg)
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

    int wd = watch_option_dir(fd);

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

            wd = watch_option_dir(fd);

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
