#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "printf_with_time.h"
#include "read_option_file.h"
#include "thermal_mount.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

void sync_thermal_mount_mode(int is_charging, MountModeState *state)
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

const char *meizu_thermal_scheme_name(int scheme)
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

void mount_thermal_files(void)
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

void umount_thermal_files(void)
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
