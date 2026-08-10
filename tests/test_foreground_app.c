#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "stub_options.h"
#include "tc_test.h"

/* 旁路应用列表是固定的 /data/adb 路径，改名后包含实现，
 * 让列表读取落到临时目录，并覆盖其中的 static 逻辑。 */
static char tc_bypass_charge_file[512 + 64];

#define bypass_charge_file tc_bypass_charge_file

#include "foreground_app.c"

#undef bypass_charge_file

#include <stdint.h>
#include <sys/stat.h>

static char fake_bin_dir[512];

static void write_executable(const char *path, const char *content)
{
    tc_write_file(path, content);
    chmod(path, 0755);
}

/* 用临时目录里的假 getprop/dumpsys 覆盖 popen 调用的外部命令 */
static void install_fake_commands(void)
{
    if (fake_bin_dir[0]) return;

    snprintf(fake_bin_dir, sizeof(fake_bin_dir), "%s", tc_path("fake_bin"));
    tc_mkdirs(fake_bin_dir);

    char script[1024];

    snprintf(script, sizeof(script), "%s/getprop", fake_bin_dir);
    write_executable(script,
                     "#!/bin/sh\n"
                     "[ \"$TC_GETPROP_EMPTY\" = 1 ] && exit 0\n"
                     "echo \"$TC_ANDROID_VERSION\"\n");

    snprintf(script, sizeof(script), "%s/dumpsys", fake_bin_dir);
    write_executable(script,
                     "#!/bin/sh\n"
                     "[ \"$TC_DUMPSYS_EMPTY\" = 1 ] && exit 0\n"
                     "echo \"  mScreenOn=$TC_SCREEN_ON\"\n");

    char path[4096];
    snprintf(path, sizeof(path), "%s:%s", fake_bin_dir, getenv("PATH"));
    setenv("PATH", path, 1);
}

static void test_fast_parse_u32(void)
{
    uint32_t out = 0;

    TC_ASSERT_INT(fast_parse_u32("0", &out), 1);
    TC_ASSERT_INT(out, 0);

    TC_ASSERT_INT(fast_parse_u32("4294967295", &out), 1);
    TC_ASSERT_INT(out, UINT32_MAX);

    TC_ASSERT_INT(fast_parse_u32("1234", &out), 1);
    TC_ASSERT_INT(out, 1234);

    /* 溢出、空串、非数字都要拒绝 */
    TC_ASSERT_INT(fast_parse_u32("4294967296", &out), 0);
    TC_ASSERT_INT(fast_parse_u32("99999999999", &out), 0);
    TC_ASSERT_INT(fast_parse_u32("12a", &out), 0);
    TC_ASSERT_INT(fast_parse_u32(" 12", &out), 0);
    TC_ASSERT_INT(fast_parse_u32("", &out), 0);
    TC_ASSERT_INT(fast_parse_u32(NULL, &out), 0);
    TC_ASSERT_INT(fast_parse_u32("1", NULL), 0);
}

static void test_fast_append_u32(void)
{
    char buf[8];

    char *p = fast_append_u32(buf, buf + sizeof(buf), 0);
    *p = '\0';
    TC_ASSERT_STR(buf, "0");

    p = fast_append_u32(buf, buf + sizeof(buf), 987654);
    *p = '\0';
    TC_ASSERT_STR(buf, "987654");

    /* 缓冲区不足时按可写空间截断 */
    p = fast_append_u32(buf, buf + 3, 123456);
    *p = '\0';
    TC_ASSERT_STR(buf, "123");

    TC_ASSERT(fast_append_u32(NULL, buf + 1, 1) == NULL);
    TC_ASSERT(fast_append_u32(buf, buf, 1) == buf);
}

static void test_fast_proc_path(void)
{
    char buf[64];

    TC_ASSERT_INT(fast_proc_path(buf, sizeof(buf), 1234, "/cmdline"), 0);
    TC_ASSERT_STR(buf, "/proc/1234/cmdline");

    TC_ASSERT_INT(fast_proc_path(buf, sizeof(buf), 0, "/stat"), 0);
    TC_ASSERT_STR(buf, "/proc/0/stat");

    /* 参数非法或空间不足时返回失败 */
    TC_ASSERT_INT(fast_proc_path(buf, 8, 1234, "/cmdline"), -1);
    TC_ASSERT_INT(fast_proc_path(buf, 4, 1234, "/cmdline"), -1);
    TC_ASSERT_INT(fast_proc_path(buf, sizeof(buf), -1, "/cmdline"), -1);
    TC_ASSERT_INT(fast_proc_path(buf, sizeof(buf), 1, NULL), -1);
    TC_ASSERT_INT(fast_proc_path(NULL, sizeof(buf), 1, "/cmdline"), -1);
    TC_ASSERT_INT(fast_proc_path(buf, 0, 1, "/cmdline"), -1);
}

static void test_log_foreground_error_once(void)
{
    int last_error = 0;

    log_foreground_error_once(&last_error, 3, "错误");
    TC_ASSERT_INT(last_error, 3);

    /* 同一错误码不重复更新 */
    log_foreground_error_once(&last_error, 3, "错误");
    TC_ASSERT_INT(last_error, 3);

    log_foreground_error_once(&last_error, 1, "另一个错误");
    TC_ASSERT_INT(last_error, 1);

    log_foreground_error_once(NULL, 1, "错误");
    log_foreground_error_once(&last_error, 2, NULL);
    TC_ASSERT_INT(last_error, 1);
}

static void test_foreground_app_accessors(void)
{
    char buf[APP_PACKAGE_NAME_MAX_SIZE];

    set_foreground_app("com.example.app");
    get_foreground_app(buf, sizeof(buf));
    TC_ASSERT_STR(buf, "com.example.app");

    set_foreground_app(NULL);
    get_foreground_app(buf, sizeof(buf));
    TC_ASSERT_STR(buf, "");

    /* 输出缓冲区较小时截断而不溢出 */
    set_foreground_app("com.example.app");
    char small[6];
    get_foreground_app(small, sizeof(small));
    TC_ASSERT_STR(small, "com.e");

    set_foreground_app("");
}

static void test_stop_flag(void)
{
    pthread_mutex_lock(&mutex_thread);
    foreground_thread_running = 0;
    foreground_thread_stop = 0;
    pthread_mutex_unlock(&mutex_thread);

    /* 线程未运行时置停止标记无效 */
    stop_foreground_thread();
    TC_ASSERT_INT(should_stop_foreground_thread(), 0);

    pthread_mutex_lock(&mutex_thread);
    foreground_thread_running = 1;
    pthread_mutex_unlock(&mutex_thread);

    stop_foreground_thread();
    TC_ASSERT_INT(should_stop_foreground_thread(), 1);

    pthread_mutex_lock(&mutex_thread);
    foreground_thread_running = 0;
    foreground_thread_stop = 0;
    pthread_mutex_unlock(&mutex_thread);
}

static void set_bypass_file(const char *sub_dir)
{
    snprintf(tc_bypass_charge_file, sizeof(tc_bypass_charge_file),
             "%s/bypass_charge.txt", tc_path(sub_dir));
    unlink(tc_bypass_charge_file);
}

static void test_load_bypass_app_list_missing_file(void)
{
    char **apps = NULL;
    int num = 0;
    time_t mtime = 0;

    set_bypass_file("bypass_missing");

    TC_ASSERT_INT(load_bypass_app_list(&apps, &num, &mtime), 0);
    TC_ASSERT(apps == NULL);
    TC_ASSERT_INT(num, 0);
}

static void test_load_bypass_app_list_parses(void)
{
    char **apps = NULL;
    int num = 0;
    time_t mtime = 0;

    set_bypass_file("bypass_parse");
    tc_write_file(tc_bypass_charge_file,
                  "# 注释行\n"
                  "\n"
                  "   \t\n"
                  "com.game.one\n"
                  "\tcom.game.two\n"
                  "  # 缩进注释\n"
                  "com.game.three\n");

    TC_ASSERT_INT(load_bypass_app_list(&apps, &num, &mtime), 1);
    TC_ASSERT_INT(num, 3);
    TC_ASSERT_STR(apps[0], "com.game.one");
    TC_ASSERT_STR(apps[1], "com.game.two");
    TC_ASSERT_STR(apps[2], "com.game.three");
    TC_ASSERT(mtime != 0);

    /* 文件未变化时直接复用缓存 */
    time_t previous = mtime;
    TC_ASSERT_INT(load_bypass_app_list(&apps, &num, &mtime), 1);
    TC_ASSERT_INT(num, 3);
    TC_ASSERT_INT(mtime, previous);

    free_string_array(&apps, num);
    num = 0;
}

static void test_load_bypass_app_list_reloads_on_change(void)
{
    char **apps = NULL;
    int num = 0;
    time_t mtime = 0;

    set_bypass_file("bypass_reload");
    tc_write_file(tc_bypass_charge_file, "com.game.one\n");
    TC_ASSERT_INT(load_bypass_app_list(&apps, &num, &mtime), 1);
    TC_ASSERT_INT(num, 1);

    /* 修改文件后重新读取，旧列表被释放 */
    struct timespec times[2] = {{0, 0}, {0, 0}};
    times[0].tv_sec = time(NULL) + 5;
    times[1].tv_sec = time(NULL) + 5;
    tc_write_file(tc_bypass_charge_file, "com.game.a\ncom.game.b\n");
    utimensat(AT_FDCWD, tc_bypass_charge_file, times, 0);

    TC_ASSERT_INT(load_bypass_app_list(&apps, &num, &mtime), 1);
    TC_ASSERT_INT(num, 2);
    TC_ASSERT_STR(apps[0], "com.game.a");
    TC_ASSERT_STR(apps[1], "com.game.b");

    free_string_array(&apps, num);
    num = 0;
}

static void test_load_bypass_app_list_grows(void)
{
    char **apps = NULL;
    int num = 0;
    time_t mtime = 0;

    set_bypass_file("bypass_grow");

    char content[4096] = {0};
    for (int i = 0; i < 20; i++) {
        char line[64];
        snprintf(line, sizeof(line), "com.app.%d\n", i);
        strncat(content, line, sizeof(content) - strlen(content) - 1);
    }
    tc_write_file(tc_bypass_charge_file, content);

    /* 初始容量为 8，需要扩容到 20 */
    TC_ASSERT_INT(load_bypass_app_list(&apps, &num, &mtime), 1);
    TC_ASSERT_INT(num, 20);
    TC_ASSERT_STR(apps[19], "com.app.19");

    free_string_array(&apps, num);
}

static void test_load_bypass_app_list_empty_file(void)
{
    char **apps = NULL;
    int num = 0;
    time_t mtime = 0;

    set_bypass_file("bypass_empty");
    tc_write_file(tc_bypass_charge_file, "# 只有注释\n\n");

    TC_ASSERT_INT(load_bypass_app_list(&apps, &num, &mtime), 1);
    TC_ASSERT_INT(num, 0);
    TC_ASSERT(apps == NULL);
}

static void test_read_proc_cmdline(void)
{
    char cmd[APP_PACKAGE_NAME_MAX_SIZE];

    /* 用自身进程验证 /proc 读取 */
    TC_ASSERT_INT(read_proc_cmdline((int)getpid(), cmd, sizeof(cmd)), 1);
    TC_ASSERT(cmd[0] != '\0');
    TC_ASSERT(strstr(cmd, "test_foreground_app") != NULL);

    /* 不存在的进程与非法参数 */
    TC_ASSERT_INT(read_proc_cmdline(2147483647, cmd, sizeof(cmd)), 0);
    TC_ASSERT_INT(read_proc_cmdline((int)getpid(), NULL, sizeof(cmd)), 0);
    TC_ASSERT_INT(read_proc_cmdline((int)getpid(), cmd, 0), 0);
}

static void test_read_foreground_from_cpuset_missing(void)
{
    char out[APP_PACKAGE_NAME_MAX_SIZE];

    cpuset_foreground_paths[0] = tc_path("cpuset_missing/top-app/tasks");
    cpuset_foreground_paths[1] = tc_path("cpuset_missing/foreground/tasks");

    /* 没有任何 cpuset 分组时返回未知 */
    TC_ASSERT_INT(read_foreground_from_cpuset(NULL, 0, out, sizeof(out)), -1);
    TC_ASSERT_INT(read_foreground_from_cpuset(NULL, 0, NULL, sizeof(out)), -1);
    TC_ASSERT_INT(read_foreground_from_cpuset(NULL, 0, out, 0), -1);
}

static void test_read_foreground_from_cpuset_first_process(void)
{
    char out[APP_PACKAGE_NAME_MAX_SIZE];
    char self[APP_PACKAGE_NAME_MAX_SIZE];
    char tasks[1024];
    char content[128];

    TC_ASSERT_INT(read_proc_cmdline((int)getpid(), self, sizeof(self)), 1);

    snprintf(tasks, sizeof(tasks), "%s", tc_path("cpuset_first/top-app/tasks"));
    snprintf(content, sizeof(content), "%d\n", (int)getpid());
    tc_write_file(tasks, content);

    cpuset_foreground_paths[0] = tasks;
    cpuset_foreground_paths[1] = tc_path("cpuset_first/foreground/tasks");

    /* 列表为空时取第一个能读到命令行的进程 */
    TC_ASSERT_INT(read_foreground_from_cpuset(NULL, 0, out, sizeof(out)), 0);
    TC_ASSERT_STR(out, self);
}

static void test_read_foreground_from_cpuset_prefers_list(void)
{
    char out[APP_PACKAGE_NAME_MAX_SIZE];
    char self[APP_PACKAGE_NAME_MAX_SIZE];
    char tasks[1024];
    char content[256];

    TC_ASSERT_INT(read_proc_cmdline((int)getpid(), self, sizeof(self)), 1);

    /* 混入非法行、不存在的进程，最后才是自身进程 */
    snprintf(tasks, sizeof(tasks), "%s", tc_path("cpuset_list/top-app/tasks"));
    snprintf(content, sizeof(content),
             "not_a_pid\n"
             "4294967295\n"
             "2147483646\n"
             "%d\n", (int)getpid());
    tc_write_file(tasks, content);

    cpuset_foreground_paths[0] = tasks;
    cpuset_foreground_paths[1] = tc_path("cpuset_list/foreground/tasks");

    char *apps[] = {NULL, self};
    TC_ASSERT_INT(read_foreground_from_cpuset(apps, 2, out, sizeof(out)), 1);
    TC_ASSERT_STR(out, self);
}

static void test_check_android_version(void)
{
    install_fake_commands();

    setenv("TC_GETPROP_EMPTY", "0", 1);

    setenv("TC_ANDROID_VERSION", "14", 1);
    TC_ASSERT_INT(check_android_version(), 14);

    setenv("TC_ANDROID_VERSION", "7", 1);
    TC_ASSERT_INT(check_android_version(), 7);

    /* 低于安卓 7 视为不可用 */
    setenv("TC_ANDROID_VERSION", "6", 1);
    TC_ASSERT_INT(check_android_version(), 0);

    /* 非数字与空输出都视为不可用 */
    setenv("TC_ANDROID_VERSION", "unknown", 1);
    TC_ASSERT_INT(check_android_version(), 0);

    setenv("TC_GETPROP_EMPTY", "1", 1);
    TC_ASSERT_INT(check_android_version(), 0);
    setenv("TC_GETPROP_EMPTY", "0", 1);
}

/* 等待前台应用名变为期望值，最多约 3 秒 */
static int wait_for_foreground(const char *expected)
{
    char buf[APP_PACKAGE_NAME_MAX_SIZE];

    for (int i = 0; i < 300; i++) {
        get_foreground_app(buf, sizeof(buf));
        if (strcmp(buf, expected) == 0) return 1;
        usleep(10 * 1000);
    }

    return 0;
}

/* 线程的轮询间隔是 FOREGROUND_POLL_SECONDS，退出最多要等一个周期 */
static int wait_for_thread_exit(void)
{
    for (int i = 0; i < 100 * (FOREGROUND_POLL_SECONDS + 5); i++) {
        pthread_mutex_lock(&mutex_thread);
        int running = foreground_thread_running;
        pthread_mutex_unlock(&mutex_thread);

        if (!running) return 1;
        usleep(10 * 1000);
    }

    return 0;
}

static void test_thread_exits_when_bypass_disabled(void)
{
    stub_options_reset();
    stub_options_set("BYPASS_CHARGE", 0);
    set_foreground_app("com.example.app");

    /* 旁路开关关闭时线程启动后立即退出并清空前台应用 */
    start_foreground_thread_if_needed(13);
    TC_ASSERT_INT(wait_for_thread_exit(), 1);

    char buf[APP_PACKAGE_NAME_MAX_SIZE];
    get_foreground_app(buf, sizeof(buf));
    TC_ASSERT_STR(buf, "");

    pthread_mutex_lock(&mutex_thread);
    TC_ASSERT_INT(foreground_thread_stop, 0);
    pthread_mutex_unlock(&mutex_thread);
}

static void test_thread_reports_screen_off(void)
{
    install_fake_commands();

    stub_options_reset();
    stub_options_set("BYPASS_CHARGE", 1);
    setenv("TC_DUMPSYS_EMPTY", "0", 1);
    setenv("TC_SCREEN_ON", "false", 1);
    set_foreground_app("");

    start_foreground_thread_if_needed(13);

    /* 息屏时上报固定标记 */
    TC_ASSERT_INT(wait_for_foreground("screen_is_off"), 1);

    /* 线程已在运行，重复调用不会再启动一个 */
    start_foreground_thread_if_needed(13);

    stub_options_set("BYPASS_CHARGE", 0);
    stop_foreground_thread();
    TC_ASSERT_INT(should_stop_foreground_thread(), 1);
}

static void test_thread_reports_foreground_package(void)
{
    char self[APP_PACKAGE_NAME_MAX_SIZE];
    char tasks[1024];
    char content[128];

    install_fake_commands();

    /* 等待上一个测试的线程退出（轮询间隔 10 秒） */
    TC_ASSERT_INT(wait_for_thread_exit(), 1);

    TC_ASSERT_INT(read_proc_cmdline((int)getpid(), self, sizeof(self)), 1);

    snprintf(tasks, sizeof(tasks), "%s", tc_path("cpuset_thread/top-app/tasks"));
    snprintf(content, sizeof(content), "%d\n", (int)getpid());
    tc_write_file(tasks, content);

    cpuset_foreground_paths[0] = tasks;
    cpuset_foreground_paths[1] = tc_path("cpuset_thread/foreground/tasks");

    set_bypass_file("bypass_thread");
    tc_write_file(tc_bypass_charge_file, self);

    stub_options_reset();
    stub_options_set("BYPASS_CHARGE", 1);
    setenv("TC_SCREEN_ON", "true", 1);
    set_foreground_app("");

    start_foreground_thread_if_needed(13);

    /* 亮屏时上报 top-app cpuset 中命中列表的应用 */
    TC_ASSERT_INT(wait_for_foreground(self), 1);

    stub_options_set("BYPASS_CHARGE", 0);
    stop_foreground_thread();
}

int main(void)
{
    TC_RUN(test_fast_parse_u32);
    TC_RUN(test_fast_append_u32);
    TC_RUN(test_fast_proc_path);
    TC_RUN(test_log_foreground_error_once);
    TC_RUN(test_foreground_app_accessors);
    TC_RUN(test_stop_flag);
    TC_RUN(test_load_bypass_app_list_missing_file);
    TC_RUN(test_load_bypass_app_list_parses);
    TC_RUN(test_load_bypass_app_list_reloads_on_change);
    TC_RUN(test_load_bypass_app_list_grows);
    TC_RUN(test_load_bypass_app_list_empty_file);
    TC_RUN(test_read_proc_cmdline);
    TC_RUN(test_read_foreground_from_cpuset_missing);
    TC_RUN(test_read_foreground_from_cpuset_first_process);
    TC_RUN(test_read_foreground_from_cpuset_prefers_list);
    TC_RUN(test_check_android_version);
    TC_RUN(test_thread_exits_when_bypass_disabled);
    TC_RUN(test_thread_reports_screen_off);
    TC_RUN(test_thread_reports_foreground_package);

    return tc_report("foreground_app");
}
