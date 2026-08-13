#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "fake_fs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAKE_MAX_ENTRIES 256
#define FAKE_CONTENT_MAX 256

typedef struct {
    char path[PATH_MAX];
    char content[FAKE_CONTENT_MAX];
    int is_dir;
    int readable;
    int writable;
    int writes;
} FakeEntry;

static FakeEntry entries[FAKE_MAX_ENTRIES];
static int entry_count = 0;
static int bind_result = 0;
static int mount_count = 0;
static int umount_count = 0;
static int shell_result = 0;
static int shell_count = 0;
static char last_shell_command[1024];

static FakeEntry *find_entry(const char *path)
{
    if (!path) return NULL;

    for (int i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].path, path) == 0) return &entries[i];
    }

    return NULL;
}

static FakeEntry *add_entry(const char *path, int is_dir)
{
    FakeEntry *e = find_entry(path);

    if (!e) {
        if (entry_count >= FAKE_MAX_ENTRIES) return NULL;
        e = &entries[entry_count++];
        memset(e, 0, sizeof(*e));
        snprintf(e->path, sizeof(e->path), "%s", path);
    }

    e->is_dir = is_dir;
    e->readable = 1;
    e->writable = 1;

    return e;
}

void fake_fs_reset(void)
{
    entry_count = 0;
    bind_result = 0;
    mount_count = 0;
    umount_count = 0;
    shell_result = 0;
    shell_count = 0;
    last_shell_command[0] = '\0';
    memset(entries, 0, sizeof(entries));
}

void fake_fs_add_file(const char *path, const char *content)
{
    FakeEntry *e = add_entry(path, 0);
    if (!e) return;

    snprintf(e->content, sizeof(e->content), "%s", content ? content : "");
}

void fake_fs_add_dir(const char *path)
{
    add_entry(path, 1);
}

void fake_fs_set_unreadable(const char *path)
{
    FakeEntry *e = find_entry(path);
    if (e) e->readable = 0;
}

void fake_fs_set_unwritable(const char *path)
{
    FakeEntry *e = find_entry(path);
    if (e) e->writable = 0;
}

int fake_fs_exists(const char *path)
{
    return find_entry(path) != NULL;
}

const char *fake_fs_content(const char *path)
{
    FakeEntry *e = find_entry(path);

    return e ? e->content : NULL;
}

int fake_fs_write_count(const char *path)
{
    FakeEntry *e = find_entry(path);

    return e ? e->writes : 0;
}

int fake_fs_total_writes(void)
{
    int total = 0;

    for (int i = 0; i < entry_count; i++) total += entries[i].writes;

    return total;
}

void fake_fs_set_bind_result(int result)
{
    bind_result = result;
}

int fake_fs_mount_count(void)
{
    return mount_count;
}

int fake_fs_umount_count(void)
{
    return umount_count;
}

void fake_fs_set_shell_result(int result)
{
    shell_result = result;
}

int fake_fs_shell_count(void)
{
    return shell_count;
}

const char *fake_fs_last_shell_command(void)
{
    return last_shell_command;
}

/* 以下为 file_utils.h 的替身实现 */

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
    return find_entry(file) != NULL;
}

int file_readable(const char *file)
{
    FakeEntry *e = find_entry(file);

    return e && e->readable;
}

int ensure_readable(const char *file)
{
    return file_readable(file);
}

int read_file(const char *file_path, char *buf, size_t buf_size)
{
    if (!file_path || !buf || buf_size == 0) return 0;

    buf[0] = '\0';

    FakeEntry *e = find_entry(file_path);
    if (!e || e->is_dir || !e->readable || e->content[0] == '\0') return 0;

    snprintf(buf, buf_size, "%s", e->content);
    line_feed(buf);

    return 1;
}

void ensure_dir(const char *dir)
{
    if (dir) fake_fs_add_dir(dir);
}

void resolve_mount_target(const char *path, char *out, size_t out_size)
{
    if (!path || !out || out_size == 0) return;

    snprintf(out, out_size, "%s", path);
}

int run_shell_command(const char *cmd)
{
    if (!cmd || !cmd[0]) return -1;

    shell_count++;
    snprintf(last_shell_command, sizeof(last_shell_command), "%s", cmd);

    return shell_result;
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

    *out = NULL;

    FakeEntry *dir = find_entry(path);
    if (!dir || !dir->is_dir) return 0;

    size_t prefix_len = strlen(path);
    char **list = calloc(FAKE_MAX_ENTRIES, sizeof(char *));
    if (!list) return 0;

    int count = 0;

    for (int i = 0; i < entry_count; i++) {
        const char *p = entries[i].path;

        if (strncmp(p, path, prefix_len) != 0) continue;
        if (p[prefix_len] != '/') continue;
        if (strchr(p + prefix_len + 1, '/')) continue;

        list[count] = strdup(p);
        if (!list[count]) break;
        count++;
    }

    if (count == 0) {
        free(list);
        return 0;
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

    long v = strtol(str, NULL, 10);
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

        while (i < sub_len && p[i] &&
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

    FakeEntry *e = find_entry(path);
    if (e && !e->writable) return 0;

    if (!e) e = add_entry(path, 0);
    if (!e) return 0;

    snprintf(e->content, sizeof(e->content), "%s", text);
    e->writes++;

    return 1;
}

int bind_mount_file(const char *fake, const char *target)
{
    if (!fake || !target) return 0;

    mount_count++;

    return bind_result;
}

int unbind_mount_file(const char *target)
{
    if (!target || !*target) return 0;

    umount_count++;

    return 1;
}
