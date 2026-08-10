#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "printf_with_time.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
void line_feed(char *line)
{
    if (!line) return;

    char *p = strchr(line, '\r');
    if (p) *p = '\0';

    p = strchr(line, '\n');
    if (p) *p = '\0';
}

int file_exists(const char *file)
{
    return access(file, F_OK) == 0;
}

int file_readable(const char *file)
{
    return access(file, R_OK) == 0;
}

int ensure_readable(const char *file)
{
    if (!file_exists(file)) return 0;

    if (!file_readable(file)) {
        chmod(file, 0644);
    }

    return file_readable(file);
}

int read_file(const char *file_path, char *buf, size_t buf_size)
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

void ensure_dir(const char *dir)
{
    if (!dir) return;
    mkdir(dir, 0755);
    chmod(dir, 0755);
}

void resolve_mount_target(const char *path, char *out, size_t out_size)
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

void free_string_array(char ***arr, int num)
{
    if (!arr || !*arr) return;

    for (int i = 0; i < num; i++) {
        free((*arr)[i]);
        (*arr)[i] = NULL;
    }

    free(*arr);
    *arr = NULL;
}

int list_dir(const char *path, char ***out)
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

int parse_non_negative_int(const char *str, int *out)
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

int contains_ignore_case(const char *s, const char *sub)
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

int ends_with(const char *s, const char *suffix)
{
    if (!s || !suffix) return 0;

    size_t s_len = strlen(s);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > s_len) return 0;

    return strcmp(s + s_len - suffix_len, suffix) == 0;
}

int write_text_file(const char *path, const char *text)
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

int bind_mount_file(const char *fake, const char *target)
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

int unbind_mount_file(const char *target)
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
