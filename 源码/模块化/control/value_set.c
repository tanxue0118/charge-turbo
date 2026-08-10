#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "printf_with_time.h"
#include "value_set.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void set_value(const char *file, const char *value)
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

void set_array_value(char **files, int num, const char *value)
{
    if (!files || num <= 0 || !value) return;

    for (int i = 0; i < num; i++) {
        if (files[i]) set_value(files[i], value);
    }
}

#define MEIZU_WIRED_LEVEL_MODE_UNLOCKED 0644
#define MEIZU_WIRED_LEVEL_MODE_LOCKED 0444

static int write_meizu_wired_level_node(const char *path, int level)
{
    if (!path) return -1;

    if (chmod(path, MEIZU_WIRED_LEVEL_MODE_UNLOCKED) != 0) {
        printf_with_time("Meizu wired_level chmod-before-write failed, path=%s, level=%d, errno=%s",
                         path, level, strerror(errno));
    }

    char value[16] = {0};
    int len = snprintf(value, sizeof(value), "%d", level);

    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        printf_with_time("Meizu wired_level open failed, path=%s, level=%d, errno=%s",
                         path, level, strerror(errno));
        return MEIZU_LEVEL_WRITE_FAILED;
    }

    ssize_t written = write(fd, value, (size_t)len);
    close(fd);

    if (written != (ssize_t)len) {
        printf_with_time("Meizu wired_level write failed, path=%s, level=%d, errno=%s",
                         path, level, strerror(errno));
        return MEIZU_LEVEL_WRITE_FAILED;
    }

    if (chmod(path, MEIZU_WIRED_LEVEL_MODE_LOCKED) != 0) {
        printf_with_time("Meizu wired_level chmod-lock failed, path=%s, level=%d, errno=%s",
                         path, level, strerror(errno));
        return MEIZU_LEVEL_WRITE_FAILED;
    }

    return 0;
}

static int restore_meizu_wired_level_node_permission(const char *path)
{
    if (!path) return -1;

    if (chmod(path, MEIZU_WIRED_LEVEL_MODE_UNLOCKED) != 0) return -1;

    return 0;
}

static const char *meizu_wired_level_paths[] = {
    MEIZU_WIRED_LEVEL_PATH,
    MEIZU_WIRED_LEVEL_LEGACY_PATH
};

int write_meizu_wired_level(int level,
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
        printf_with_time("Meizu wired_level node found, path=%s, level=%d, method=chmod0644-write-chmod0444",
                         path, level);
        int ret = write_meizu_wired_level_node(path, level);
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
            printf_with_time("Meizu wired_level chmod 0644 failed, path=%s, ret=%d", path, ret);
        } else {
            printf_with_time("Meizu wired_level chmod 0644 success, path=%s", path);
        }
    }

    if (found == 0) {
        printf_with_time("Meizu wired_level chmod 0644 skipped, nodes missing, paths=%s",
                         MEIZU_WIRED_LEVEL_PATHS_TEXT);
        return MEIZU_LEVEL_NODE_MISSING;
    }
    if (failed > 0) return -1;

    return 0;
}
