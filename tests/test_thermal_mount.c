#define _GNU_SOURCE

#include "global.h"
#include "fake_fs.h"
#include "stub_options.h"
#include "tc_test.h"

#include <sys/stat.h>

/* 挂载源目录和状态目录都是固定的 /data/adb 路径：
 * 魅族方案目录用宏重定向到临时目录，THERMAL_FILES_DIR 在实现内部定义，
 * 由 PATH 中的假 find 把它翻译到临时目录（见 install_fake_find）。 */
#undef STATE_DIR
#define STATE_DIR "/tmp/tc_test_thermal/state"

#undef MEIZU_THERMAL_FLYME_CLEAR_DIR
#undef MEIZU_THERMAL_EXTREMEGT_DIR
#define MEIZU_THERMAL_FLYME_CLEAR_DIR "/tmp/tc_test_thermal/flyme_clear"
#define MEIZU_THERMAL_EXTREMEGT_DIR "/tmp/tc_test_thermal/extremegt"

#include "thermal_mount.c"

#define DEFAULT_DIR "/data/adb/modules/turbo-charge/thermal_files"
#define EMPTY_FILE STATE_DIR "/.empty"

static char fake_bin_dir[512];

/* 实现按 strlen(thermal_dir) 截取相对路径，
 * 所以替身目录的长度必须和 THERMAL_FILES_DIR 完全一致。 */
static const char *default_dir_alias(void)
{
    static char buf[256];

    if (!buf[0]) {
        size_t want = strlen(DEFAULT_DIR);

        snprintf(buf, sizeof(buf), "/tmp/tc_test_thermal/default");

        while (strlen(buf) < want && strlen(buf) + 1 < sizeof(buf)) {
            strcat(buf, "_");
        }

        buf[want] = '\0';
    }

    return buf;
}

/* 假 find 把实现里硬编码的 THERMAL_FILES_DIR 换成 TC_THERMAL_DIR 指向的临时目录 */
static void install_fake_find(void)
{
    if (fake_bin_dir[0]) return;

    snprintf(fake_bin_dir, sizeof(fake_bin_dir), "%s", tc_path("fake_bin"));
    tc_mkdirs(fake_bin_dir);

    char script[1024];
    snprintf(script, sizeof(script), "%s/find", fake_bin_dir);

    tc_write_file(script,
                  "#!/bin/sh\n"
                  "dir=\"$1\"\n"
                  "shift\n"
                  "if [ \"$dir\" = \"" DEFAULT_DIR "\" ]; then dir=\"$TC_THERMAL_DIR\"; fi\n"
                  "exec /usr/bin/find \"$dir\" \"$@\"\n");
    chmod(script, 0755);

    char path[4096];
    snprintf(path, sizeof(path), "%s:%s", fake_bin_dir, getenv("PATH"));
    setenv("PATH", path, 1);
}

static void reset_all(void)
{
    umount_thermal_files();
    fake_fs_reset();
    stub_options_reset();
    unsetenv("TC_THERMAL_DIR");
}

/* 在真实临时目录里放一个挂载源文件，并把它对应的 /sys 目标登记进 fake_fs */
static void add_thermal_pair(const char *real_dir, const char *relative, int target_exists)
{
    char real_path[1024];
    snprintf(real_path, sizeof(real_path), "%s%s", real_dir, relative);
    tc_write_file(real_path, "0\n");

    if (target_exists) fake_fs_add_file(relative, "1");
}

static void prepare_state_dir(int empty_file_creatable)
{
    tc_mkdirs(STATE_DIR);
    fake_fs_add_dir(STATE_DIR);

    if (empty_file_creatable) fake_fs_add_file(EMPTY_FILE, "");
}

static void test_meizu_thermal_scheme_name(void)
{
    TC_ASSERT_STR(meizu_thermal_scheme_name(MEIZU_THERMAL_SCHEME_FLYME_CLEAR), "Flyme clear");
    TC_ASSERT_STR(meizu_thermal_scheme_name(MEIZU_THERMAL_SCHEME_EXTREMEGT), "extremegt relaxed");
    TC_ASSERT_STR(meizu_thermal_scheme_name(0), "extremegt relaxed");
    TC_ASSERT_STR(meizu_thermal_scheme_name(99), "extremegt relaxed");
}

static void test_select_thermal_files_dir(void)
{
    reset_all();

    int meizu = -1;
    int scheme = -1;

    /* 非魅族设备使用通用目录 */
    stub_options_set("MEIZU_DEVICE", 0);
    TC_ASSERT_STR(select_thermal_files_dir(&meizu, &scheme), DEFAULT_DIR);
    TC_ASSERT_INT(meizu, 0);

    /* 魅族设备按方案选目录，非法方案被夹到合法范围 */
    stub_options_set("MEIZU_DEVICE", 1);
    stub_options_set("MEIZU_THERMAL_SCHEME", MEIZU_THERMAL_SCHEME_FLYME_CLEAR);
    TC_ASSERT_STR(select_thermal_files_dir(&meizu, &scheme), MEIZU_THERMAL_FLYME_CLEAR_DIR);
    TC_ASSERT_INT(meizu, 1);
    TC_ASSERT_INT(scheme, MEIZU_THERMAL_SCHEME_FLYME_CLEAR);

    stub_options_set("MEIZU_THERMAL_SCHEME", MEIZU_THERMAL_SCHEME_EXTREMEGT);
    TC_ASSERT_STR(select_thermal_files_dir(&meizu, &scheme), MEIZU_THERMAL_EXTREMEGT_DIR);
    TC_ASSERT_INT(scheme, MEIZU_THERMAL_SCHEME_EXTREMEGT);

    stub_options_set("MEIZU_THERMAL_SCHEME", 99);
    TC_ASSERT_STR(select_thermal_files_dir(NULL, NULL), MEIZU_THERMAL_EXTREMEGT_DIR);
}

static void test_thermal_mount_helpers(void)
{
    TC_ASSERT_INT(should_mount_thermal_target("/sys/class/thermal/x"), 1);
    TC_ASSERT_INT(should_mount_thermal_target(""), 0);
    TC_ASSERT_INT(should_mount_thermal_target(NULL), 0);

    TC_ASSERT_STR(thermal_mount_source_for_target("/sys/x", EMPTY_FILE), EMPTY_FILE);
    TC_ASSERT_STR(thermal_mount_source_for_target(NULL, EMPTY_FILE), EMPTY_FILE);
    TC_ASSERT(thermal_mount_source_for_target("/sys/x", NULL) == NULL);
}

static void test_list_dir_recursive(void)
{
    char **files = NULL;
    const char *dir = tc_path("recursive");
    char nested[1024];

    snprintf(nested, sizeof(nested), "%s/a/b", dir);
    tc_mkdirs(nested);

    char file[1200];
    snprintf(file, sizeof(file), "%s/a/one", dir);
    tc_write_file(file, "1");
    snprintf(file, sizeof(file), "%s/a/b/two", dir);
    tc_write_file(file, "2");

    int count = list_dir_recursive(dir, &files);
    TC_ASSERT_INT(count, 2);
    TC_ASSERT(files != NULL);
    free_string_array(&files, count);

    /* 空目录与不存在的目录返回 0 */
    const char *empty_dir = tc_path("recursive_empty");
    tc_mkdirs(empty_dir);
    TC_ASSERT_INT(list_dir_recursive(empty_dir, &files), 0);
    TC_ASSERT(files == NULL);

    TC_ASSERT_INT(list_dir_recursive(tc_path("recursive_missing"), &files), 0);

    TC_ASSERT_INT(list_dir_recursive(NULL, &files), 0);
    TC_ASSERT_INT(list_dir_recursive(dir, NULL), 0);
}

static void test_list_dir_recursive_grows(void)
{
    char **files = NULL;
    const char *dir = tc_path("recursive_many");
    char file[1200];

    tc_mkdirs(dir);

    /* 初始容量 64，需要扩容 */
    for (int i = 0; i < 80; i++) {
        snprintf(file, sizeof(file), "%s/f%d", dir, i);
        tc_write_file(file, "x");
    }

    int count = list_dir_recursive(dir, &files);
    TC_ASSERT_INT(count, 80);
    free_string_array(&files, count);
}

static void test_mount_missing_dir(void)
{
    reset_all();

    stub_options_set("MEIZU_DEVICE", 1);
    stub_options_set("MEIZU_THERMAL_SCHEME", MEIZU_THERMAL_SCHEME_EXTREMEGT);

    /* 温控目录不存在时不做任何挂载 */
    mount_thermal_files();
    TC_ASSERT_INT(mounted_thermal_count, 0);
    TC_ASSERT_INT(fake_fs_mount_count(), 0);
}

static void test_mount_without_empty_file(void)
{
    reset_all();
    install_fake_find();

    const char *dir = MEIZU_THERMAL_EXTREMEGT_DIR;
    tc_mkdirs(dir);
    fake_fs_add_dir(dir);
    prepare_state_dir(0);

    stub_options_set("MEIZU_DEVICE", 1);
    stub_options_set("MEIZU_THERMAL_SCHEME", MEIZU_THERMAL_SCHEME_EXTREMEGT);

    /* 空文件创建失败时跳过挂载 */
    mount_thermal_files();
    TC_ASSERT_INT(mounted_thermal_count, 0);
    TC_ASSERT_INT(fake_fs_mount_count(), 0);
}

static void test_mount_empty_dir(void)
{
    reset_all();
    install_fake_find();

    const char *dir = MEIZU_THERMAL_FLYME_CLEAR_DIR;
    tc_mkdirs(dir);
    fake_fs_add_dir(dir);
    prepare_state_dir(1);

    stub_options_set("MEIZU_DEVICE", 1);
    stub_options_set("MEIZU_THERMAL_SCHEME", MEIZU_THERMAL_SCHEME_FLYME_CLEAR);

    /* 目录里没有文件时跳过挂载 */
    mount_thermal_files();
    TC_ASSERT_INT(mounted_thermal_count, 0);
}

static void test_mount_meizu_files(void)
{
    reset_all();
    install_fake_find();

    const char *dir = MEIZU_THERMAL_EXTREMEGT_DIR;
    tc_mkdirs(dir);
    fake_fs_add_dir(dir);
    prepare_state_dir(1);

    /* 一个目标存在、一个目标不存在 */
    add_thermal_pair(dir, "/sys/class/thermal/present", 1);
    add_thermal_pair(dir, "/sys/class/thermal/absent", 0);

    fake_fs_set_bind_result(1);
    stub_options_set("MEIZU_DEVICE", 1);
    stub_options_set("MEIZU_THERMAL_SCHEME", MEIZU_THERMAL_SCHEME_EXTREMEGT);

    mount_thermal_files();

    /* 只挂载目标已存在的文件 */
    TC_ASSERT_INT(mounted_thermal_count, 1);
    TC_ASSERT_STR(mounted_thermal_paths[0], "/sys/class/thermal/present");
    TC_ASSERT_INT(fake_fs_mount_count(), 1);

    /* 卸载后计数清零 */
    umount_thermal_files();
    TC_ASSERT_INT(mounted_thermal_count, 0);
    TC_ASSERT_INT(fake_fs_umount_count(), 1);
    TC_ASSERT(mounted_thermal_paths[0] == NULL);

    /* 已无挂载时再次卸载无副作用 */
    umount_thermal_files();
    TC_ASSERT_INT(fake_fs_umount_count(), 1);
}

static void test_mount_bind_failure(void)
{
    reset_all();
    install_fake_find();

    const char *dir = MEIZU_THERMAL_EXTREMEGT_DIR;
    tc_mkdirs(dir);
    fake_fs_add_dir(dir);
    prepare_state_dir(1);
    add_thermal_pair(dir, "/sys/class/thermal/present", 1);

    fake_fs_set_bind_result(0);
    stub_options_set("MEIZU_DEVICE", 1);

    /* 挂载失败时不记录路径 */
    mount_thermal_files();
    TC_ASSERT_INT(mounted_thermal_count, 0);
    TC_ASSERT_INT(fake_fs_mount_count(), 1);
}

static void test_mount_default_dir(void)
{
    reset_all();
    install_fake_find();

    const char *dir = default_dir_alias();
    tc_mkdirs(dir);
    setenv("TC_THERMAL_DIR", dir, 1);

    fake_fs_add_dir(DEFAULT_DIR);
    prepare_state_dir(1);
    add_thermal_pair(dir, "/sys/class/thermal/generic", 1);

    fake_fs_set_bind_result(1);
    stub_options_set("MEIZU_DEVICE", 0);

    /* 非魅族设备用空文件作为挂载源 */
    mount_thermal_files();
    TC_ASSERT_INT(mounted_thermal_count, 1);
    TC_ASSERT_STR(mounted_thermal_paths[0], "/sys/class/thermal/generic");

    umount_thermal_files();
    TC_ASSERT_INT(mounted_thermal_count, 0);
}

/* 准备一个可成功挂载的魅族目录，供 sync 用例复用 */
static void prepare_mountable_dir(void)
{
    const char *dir = MEIZU_THERMAL_EXTREMEGT_DIR;

    tc_mkdirs(dir);
    fake_fs_add_dir(dir);
    prepare_state_dir(1);
    add_thermal_pair(dir, "/sys/class/thermal/present", 1);

    fake_fs_set_bind_result(1);
    stub_options_set("MEIZU_DEVICE", 1);
}

static void test_sync_always_mode(void)
{
    reset_all();
    install_fake_find();
    prepare_mountable_dir();

    MountModeState state = {0};
    stub_options_set("THERMAL_MOUNT_MODE", 0);

    /* 常驻模式：无论是否充电都保持挂载 */
    sync_thermal_mount_mode(0, &state);
    TC_ASSERT_INT(state.mounted, 1);
    TC_ASSERT_INT(state.charging, 0);
    TC_ASSERT_INT(state.last_mode, 0);
    TC_ASSERT_INT(fake_fs_mount_count(), 1);

    /* 已挂载时不重复挂载 */
    sync_thermal_mount_mode(1, &state);
    TC_ASSERT_INT(state.mounted, 1);
    TC_ASSERT_INT(state.charging, 1);
    TC_ASSERT_INT(fake_fs_mount_count(), 1);

    umount_thermal_files();
}

static void test_sync_charging_only_mode(void)
{
    reset_all();
    install_fake_find();
    prepare_mountable_dir();

    MountModeState state = {0};
    stub_options_set("THERMAL_MOUNT_MODE", 1);

    /* 仅充电模式：未充电时不挂载 */
    sync_thermal_mount_mode(0, &state);
    TC_ASSERT_INT(state.mounted, 0);
    TC_ASSERT_INT(fake_fs_mount_count(), 0);

    sync_thermal_mount_mode(1, &state);
    TC_ASSERT_INT(state.mounted, 1);
    TC_ASSERT_INT(mounted_thermal_count, 1);

    /* 停止充电后卸载 */
    sync_thermal_mount_mode(0, &state);
    TC_ASSERT_INT(state.mounted, 0);
    TC_ASSERT_INT(mounted_thermal_count, 0);
    TC_ASSERT_INT(fake_fs_umount_count(), 1);
}

static void test_sync_invalid_mode_and_null(void)
{
    reset_all();
    install_fake_find();

    MountModeState state = {0};

    /* 非法模式按常驻模式处理；目录不存在时保持未挂载 */
    stub_options_set("THERMAL_MOUNT_MODE", 5);
    sync_thermal_mount_mode(1, &state);
    TC_ASSERT_INT(state.last_mode, 0);
    TC_ASSERT_INT(state.mounted, 0);

    sync_thermal_mount_mode(1, NULL);
}

int main(void)
{
    /* 固定的临时挂载目录不能残留上一次运行的文件 */
    if (system("rm -rf /tmp/tc_test_thermal") != 0) return 2;

    TC_RUN(test_meizu_thermal_scheme_name);
    TC_RUN(test_select_thermal_files_dir);
    TC_RUN(test_thermal_mount_helpers);
    TC_RUN(test_list_dir_recursive);
    TC_RUN(test_list_dir_recursive_grows);
    TC_RUN(test_mount_missing_dir);
    TC_RUN(test_mount_without_empty_file);
    TC_RUN(test_mount_empty_dir);
    TC_RUN(test_mount_meizu_files);
    TC_RUN(test_mount_bind_failure);
    TC_RUN(test_mount_default_dir);
    TC_RUN(test_sync_always_mode);
    TC_RUN(test_sync_charging_only_mode);
    TC_RUN(test_sync_invalid_mode_and_null);

    return tc_report("thermal_mount");
}
