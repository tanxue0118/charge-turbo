#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "foreground_app.h"
#include "stub_modules.h"
#include "thermal_mount.h"
#include "value_set.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STUB_MAX_WRITES 128
#define STUB_MAX_APPS 16

typedef struct {
    char path[PATH_MAX];
    char value[128];
    int count;
} StubWrite;

static StubWrite writes[STUB_MAX_WRITES];
static int write_slots = 0;
static int write_total = 0;

static int meizu_write_result = 0;
static int meizu_write_found = 1;
static int meizu_write_success = 1;
static int meizu_write_calls = 0;
static int meizu_last_level = -1;
static int meizu_restore_result = 0;
static int meizu_restore_calls = 0;

static int mount_thermal_calls = 0;
static int umount_thermal_calls = 0;
static int sync_thermal_calls = 0;

static char foreground_name[APP_PACKAGE_NAME_MAX_SIZE];
static char bypass_apps[STUB_MAX_APPS][APP_PACKAGE_NAME_MAX_SIZE];
static int bypass_app_count = 0;
static int bypass_list_result = 1;
static int foreground_start_calls = 0;
static int foreground_stop_calls = 0;
static int android_version_result = 13;

void stub_modules_reset(void)
{
    memset(writes, 0, sizeof(writes));
    write_slots = 0;
    write_total = 0;

    meizu_write_result = 0;
    meizu_write_found = 1;
    meizu_write_success = 1;
    meizu_write_calls = 0;
    meizu_last_level = -1;
    meizu_restore_result = 0;
    meizu_restore_calls = 0;

    mount_thermal_calls = 0;
    umount_thermal_calls = 0;
    sync_thermal_calls = 0;

    foreground_name[0] = '\0';
    bypass_app_count = 0;
    bypass_list_result = 1;
    foreground_start_calls = 0;
    foreground_stop_calls = 0;
    android_version_result = 13;
}

static StubWrite *write_slot(const char *path)
{
    for (int i = 0; i < write_slots; i++) {
        if (strcmp(writes[i].path, path) == 0) return &writes[i];
    }

    if (write_slots >= STUB_MAX_WRITES) return NULL;

    StubWrite *slot = &writes[write_slots++];
    snprintf(slot->path, sizeof(slot->path), "%s", path);

    return slot;
}

int stub_set_value_count(const char *path)
{
    StubWrite *slot = write_slot(path);

    return slot ? slot->count : 0;
}

const char *stub_last_set_value(const char *path)
{
    StubWrite *slot = write_slot(path);

    return slot ? slot->value : "";
}

int stub_set_value_total(void)
{
    return write_total;
}

void stub_set_meizu_write_result(int result, int found, int success)
{
    meizu_write_result = result;
    meizu_write_found = found;
    meizu_write_success = success;
}

int stub_meizu_write_calls(void)
{
    return meizu_write_calls;
}

int stub_meizu_last_level(void)
{
    return meizu_last_level;
}

void stub_set_meizu_restore_result(int result)
{
    meizu_restore_result = result;
}

int stub_meizu_restore_calls(void)
{
    return meizu_restore_calls;
}

int stub_mount_thermal_calls(void)
{
    return mount_thermal_calls;
}

int stub_umount_thermal_calls(void)
{
    return umount_thermal_calls;
}

int stub_sync_thermal_calls(void)
{
    return sync_thermal_calls;
}

void stub_set_foreground_app_name(const char *name)
{
    snprintf(foreground_name, sizeof(foreground_name), "%s", name ? name : "");
}

void stub_set_bypass_app_list(const char **apps, int count, int result)
{
    bypass_app_count = 0;
    bypass_list_result = result;

    for (int i = 0; i < count && i < STUB_MAX_APPS; i++) {
        snprintf(bypass_apps[bypass_app_count], APP_PACKAGE_NAME_MAX_SIZE, "%s", apps[i]);
        bypass_app_count++;
    }
}

int stub_foreground_start_calls(void)
{
    return foreground_start_calls;
}

int stub_foreground_stop_calls(void)
{
    return foreground_stop_calls;
}

void stub_set_android_version(int version)
{
    android_version_result = version;
}

/* value_set 替身 */

void set_value(const char *file, const char *value)
{
    if (!file || !value) return;

    StubWrite *slot = write_slot(file);
    if (!slot) return;

    snprintf(slot->value, sizeof(slot->value), "%s", value);
    slot->count++;
    write_total++;

    /* 与假文件系统保持一致：已存在的节点内容随写入更新，
     * 便于被测代码回读校验写入结果。 */
    if (file_exists(file)) write_text_file(file, value);
}

void set_array_value(char **files, int num, const char *value)
{
    if (!files || num <= 0 || !value) return;

    for (int i = 0; i < num; i++) {
        if (files[i]) set_value(files[i], value);
    }
}

int write_meizu_wired_level_with_echo(int level, int *found_count, int *success_count)
{
    meizu_write_calls++;
    meizu_last_level = level;

    if (found_count) *found_count = meizu_write_found;
    if (success_count) *success_count = meizu_write_success;

    return meizu_write_result;
}

int restore_meizu_wired_level_permission(void)
{
    meizu_restore_calls++;

    return meizu_restore_result;
}

/* thermal_mount 替身 */

const char *meizu_thermal_scheme_name(int scheme)
{
    return scheme == MEIZU_THERMAL_SCHEME_FLYME_CLEAR ? "Flyme clear" : "extremegt relaxed";
}

void mount_thermal_files(void)
{
    mount_thermal_calls++;
}

void umount_thermal_files(void)
{
    umount_thermal_calls++;
}

void sync_thermal_mount_mode(int is_charging, MountModeState *state)
{
    sync_thermal_calls++;

    if (state) state->charging = is_charging;
}

/* foreground_app 替身 */

int check_android_version(void)
{
    return android_version_result;
}

void get_foreground_app(char *out, size_t out_size)
{
    if (out && out_size) snprintf(out, out_size, "%s", foreground_name);
}

void start_foreground_thread_if_needed(int android_version)
{
    (void)android_version;
    foreground_start_calls++;
}

void stop_foreground_thread(void)
{
    foreground_stop_calls++;
}

int load_bypass_app_list(char ***apps, int *app_num, time_t *last_mtime)
{
    if (!apps || !app_num || !last_mtime) return 0;
    if (!bypass_list_result) return 0;

    free_string_array(apps, *app_num);
    *app_num = 0;

    if (bypass_app_count == 0) return 1;

    char **list = calloc(bypass_app_count, sizeof(char *));
    if (!list) return 0;

    for (int i = 0; i < bypass_app_count; i++) {
        list[i] = strdup(bypass_apps[i]);
    }

    *apps = list;
    *app_num = bypass_app_count;
    *last_mtime = 1;

    return 1;
}
