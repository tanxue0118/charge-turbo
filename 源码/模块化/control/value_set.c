#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "printf_with_time.h"
#include "value_set.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define WRITE_NODE_ACTION "写入节点"

static int report_write_failure(const char *file, int errnum)
{
    log_io_failure(WRITE_NODE_ACTION, file, errnum);

    return SET_VALUE_FAILED;
}

int set_value(const char *file, const char *value)
{
    if (!file || !value) return SET_VALUE_SKIPPED;
    if (!file_exists(file)) return SET_VALUE_SKIPPED;

    struct stat statbuf;
    if (stat(file, &statbuf) != 0) return report_write_failure(file, errno);

    FILE *fp = fopen(file, "r+");

    if (!fp) {
        chmod(file, 0644);
        fp = fopen(file, "r+");
    }

    if (!fp) return report_write_failure(file, errno);

    char *old = calloc(1, statbuf.st_size + 1);
    if (!old) {
        fclose(fp);
        return report_write_failure(file, ENOMEM);
    }

    if (fgets(old, statbuf.st_size + 1, fp)) {
        line_feed(old);
    }

    int result = SET_VALUE_OK;

    if (strcmp(old, value) != 0) {
        rewind(fp);
        errno = 0;

        if (fputs(value, fp) < 0 || fflush(fp) != 0) {
            result = report_write_failure(file, errno ? errno : EIO);
        }
    }

    free(old);
    errno = 0;

    if (fclose(fp) != 0 && result == SET_VALUE_OK) {
        result = report_write_failure(file, errno ? errno : EIO);
    }

    if (result == SET_VALUE_OK) clear_io_failure(WRITE_NODE_ACTION, file);

    return result;
}

int set_array_value(char **files, int num, const char *value)
{
    if (!files || num <= 0 || !value) return SET_VALUE_SKIPPED;

    int ok = 0;
    int failed = 0;

    for (int i = 0; i < num; i++) {
        if (!files[i]) continue;

        int result = set_value(files[i], value);
        if (result == SET_VALUE_OK) ok++;
        else if (result == SET_VALUE_FAILED) failed++;
    }

    if (ok > 0) return SET_VALUE_OK;
    if (failed > 0) return SET_VALUE_FAILED;

    return SET_VALUE_SKIPPED;
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

int write_meizu_wired_level_with_echo(int level,
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

int restore_meizu_wired_level_permission(void)
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
