#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "tc_test.h"

/* 配置目录是固定的 /data/adb 路径。改名后包含实现，
 * 让配置读取与 inotify 监听落在临时目录，并覆盖其中的 static 函数。 */
static char tc_option_dir[512];
static char tc_option_file[512 + 128];

#define option_dir tc_option_dir
#define option_file tc_option_file

#include "read_option_file.c"

#undef option_dir
#undef option_file

#include <sys/wait.h>
#include <time.h>

static void set_option_paths(const char *sub_dir)
{
    snprintf(tc_option_dir, sizeof(tc_option_dir), "%s", tc_path(sub_dir));
    snprintf(tc_option_file, sizeof(tc_option_file), "%s/%s",
             tc_option_dir, option_name);

    tc_mkdirs(tc_option_dir);
    unlink(tc_option_file);
}

static void reset_options(void)
{
    pthread_mutex_lock(&mutex_options);
    for (int i = 0; i < option_count; i++) {
        options[i].value = options[i].default_value;
    }
    option_generation = 0;
    pthread_mutex_unlock(&mutex_options);
}

static int option_value(const char *name)
{
    return options[option_index(name)].value;
}

static void test_option_index(void)
{
    TC_ASSERT_INT(option_index("CYCLE_TIME"), 0);
    TC_ASSERT(option_index("MEIZU_THERMAL_SCHEME") > 0);
    TC_ASSERT_INT(option_index("NO_SUCH_OPTION"), -1);
    TC_ASSERT_INT(option_index(""), -1);
}

static void test_read_one_option(void)
{
    reset_options();

    TC_ASSERT_INT(read_one_option("CHARGE_STOP"), 95);

    pthread_mutex_lock(&mutex_options);
    options[option_index("CHARGE_STOP")].value = 77;
    pthread_mutex_unlock(&mutex_options);

    TC_ASSERT_INT(read_one_option("CHARGE_STOP"), 77);

    reset_options();
}

static void test_read_one_option_unknown_exits(void)
{
    fflush(stdout);

    /* 未知配置项属于内部错误，进程应以 98765 退出 */
    pid_t pid = fork();
    if (pid == 0) {
        freopen("/dev/null", "w", stdout);
        read_one_option("NO_SUCH_OPTION");
        _exit(0);
    }

    int status = 0;
    TC_ASSERT_INT(waitpid(pid, &status, 0), pid);
    TC_ASSERT_INT(WIFEXITED(status), 1);
    TC_ASSERT_INT(WEXITSTATUS(status), 98765 & 0xff);
}

static void test_read_option_generation(void)
{
    reset_options();
    TC_ASSERT_INT(read_option_generation(), 0);

    pthread_mutex_lock(&mutex_options);
    option_generation = 42;
    pthread_mutex_unlock(&mutex_options);

    TC_ASSERT_INT(read_option_generation(), 42);

    reset_options();
}

static void test_load_missing_file_keeps_defaults(void)
{
    reset_options();
    set_option_paths("opt_missing");

    load_option_file(1);

    /* 打开失败时不应递增 generation，也不应改动任何值 */
    TC_ASSERT_INT(read_option_generation(), 0);
    TC_ASSERT_INT(option_value("CHARGE_STOP"), 95);

    load_option_file(0);
    TC_ASSERT_INT(read_option_generation(), 0);
}

static void test_load_parses_values(void)
{
    reset_options();
    set_option_paths("opt_parse");

    tc_write_file(tc_option_file,
                  "CHARGE_STOP=90\n"
                  "CHARGE_START=70\n"
                  "TEMP_MAX=48\n");

    load_option_file(1);

    TC_ASSERT_INT(option_value("CHARGE_STOP"), 90);
    TC_ASSERT_INT(option_value("CHARGE_START"), 70);
    TC_ASSERT_INT(option_value("TEMP_MAX"), 48);
    TC_ASSERT_INT(read_option_generation(), 1);

    /* 未出现的配置项保持默认值 */
    TC_ASSERT_INT(option_value("TEMP_LEVEL1"), 45);

    reset_options();
}

static void test_load_ignores_comments_and_junk(void)
{
    reset_options();
    set_option_paths("opt_junk");

    tc_write_file(tc_option_file,
                  "# CHARGE_STOP=10\n"
                  "   # 缩进注释\n"
                  "\n"
                  "   \t\n"
                  "no_equal_sign\n"
                  "UNKNOWN_OPTION=5\n"
                  "\tCHARGE_STOP=88\n"
                  "TEMP_MAX=abc\n"
                  "TEMP_LEVEL1=\n"
                  "TEMP_LEVEL2=-5\n");

    load_option_file(1);

    /* 前导空白被跳过，注释、空行、缺少等号与未知项都被忽略 */
    TC_ASSERT_INT(option_value("CHARGE_STOP"), 88);

    /* 非法值沿用原值 */
    TC_ASSERT_INT(option_value("TEMP_MAX"), 52);
    TC_ASSERT_INT(option_value("TEMP_LEVEL1"), 45);
    TC_ASSERT_INT(option_value("TEMP_LEVEL2"), 50);

    reset_options();
}

static void test_load_rejects_zero_cycle_time(void)
{
    reset_options();
    set_option_paths("opt_cycle");

    tc_write_file(tc_option_file, "CYCLE_TIME=0\n");
    load_option_file(1);
    TC_ASSERT_INT(option_value("CYCLE_TIME"), 1);

    tc_write_file(tc_option_file, "CYCLE_TIME=5\n");
    load_option_file(0);
    TC_ASSERT_INT(option_value("CYCLE_TIME"), 5);

    /* 已经是 5 之后再写 0，应沿用上一次的值 */
    tc_write_file(tc_option_file, "CYCLE_TIME=0\n");
    load_option_file(0);
    TC_ASSERT_INT(option_value("CYCLE_TIME"), 5);

    reset_options();
}

static void test_load_clamps_meizu_values(void)
{
    reset_options();
    set_option_paths("opt_meizu");

    tc_write_file(tc_option_file,
                  "MEIZU_CHARGE_LEVEL=0\n"
                  "MEIZU_THERMAL_SCHEME=9\n");
    load_option_file(1);
    TC_ASSERT_INT(option_value("MEIZU_CHARGE_LEVEL"), 10);
    TC_ASSERT_INT(option_value("MEIZU_THERMAL_SCHEME"), MEIZU_THERMAL_SCHEME_EXTREMEGT);

    tc_write_file(tc_option_file,
                  "MEIZU_CHARGE_LEVEL=11\n"
                  "MEIZU_THERMAL_SCHEME=1\n");
    load_option_file(0);
    TC_ASSERT_INT(option_value("MEIZU_CHARGE_LEVEL"), 10);
    TC_ASSERT_INT(option_value("MEIZU_THERMAL_SCHEME"), MEIZU_THERMAL_SCHEME_FLYME_CLEAR);

    tc_write_file(tc_option_file,
                  "MEIZU_CHARGE_LEVEL=6\n"
                  "MEIZU_THERMAL_SCHEME=2\n");
    load_option_file(0);
    TC_ASSERT_INT(option_value("MEIZU_CHARGE_LEVEL"), 6);
    TC_ASSERT_INT(option_value("MEIZU_THERMAL_SCHEME"), 2);

    reset_options();
}

static void test_load_normalizes_mount_modes(void)
{
    reset_options();
    set_option_paths("opt_mount");

    tc_write_file(tc_option_file,
                  "TEMP_SIMULATE_MOUNT_MODE=7\n"
                  "THERMAL_MOUNT_MODE=3\n"
                  "TEMP_SIMULATE=2\n");
    load_option_file(1);

    /* 挂载模式只能是 0/1，其它非零值归一为 1 */
    TC_ASSERT_INT(option_value("TEMP_SIMULATE_MOUNT_MODE"), 1);
    TC_ASSERT_INT(option_value("THERMAL_MOUNT_MODE"), 1);

    /* 其它开关项不做归一化 */
    TC_ASSERT_INT(option_value("TEMP_SIMULATE"), 2);

    reset_options();
}

static void test_load_increments_generation(void)
{
    reset_options();
    set_option_paths("opt_gen");

    tc_write_file(tc_option_file, "CHARGE_STOP=90\n");

    load_option_file(1);
    TC_ASSERT_INT(read_option_generation(), 1);

    load_option_file(0);
    TC_ASSERT_INT(read_option_generation(), 2);

    reset_options();
}

static void test_is_option_event(void)
{
    char raw[sizeof(struct inotify_event) + 64];
    struct inotify_event *ev = (struct inotify_event *)raw;

    memset(raw, 0, sizeof(raw));
    ev->len = (uint32_t)(strlen(option_name) + 1);
    snprintf(ev->name, 64, "%s", option_name);

    ev->mask = IN_CLOSE_WRITE;
    TC_ASSERT_INT(is_option_event(ev), 1);
    ev->mask = IN_MOVED_TO;
    TC_ASSERT_INT(is_option_event(ev), 1);
    ev->mask = IN_CREATE;
    TC_ASSERT_INT(is_option_event(ev), 1);
    ev->mask = IN_ATTRIB;
    TC_ASSERT_INT(is_option_event(ev), 1);
    ev->mask = IN_OPEN;
    TC_ASSERT_INT(is_option_event(ev), 0);

    /* 其它文件名的事件被忽略 */
    snprintf(ev->name, 64, "other.txt");
    ev->len = (uint32_t)(strlen("other.txt") + 1);
    ev->mask = IN_CLOSE_WRITE;
    TC_ASSERT_INT(is_option_event(ev), 0);

    /* 目录自身的事件没有文件名，只看掩码 */
    ev->len = 0;
    ev->name[0] = '\0';
    ev->mask = IN_CLOSE_WRITE;
    TC_ASSERT_INT(is_option_event(ev), 1);

    TC_ASSERT_INT(is_option_event(NULL), 0);
}

/* 等待配置代数增长，最多等待约 2 秒 */
static int wait_for_generation(unsigned long target)
{
    for (int i = 0; i < 200; i++) {
        if (read_option_generation() >= target) return 1;
        usleep(10 * 1000);
    }

    return 0;
}

static void test_watch_thread_reloads_on_change(void)
{
    reset_options();
    set_option_paths("opt_watch");
    tc_write_file(tc_option_file, "CHARGE_STOP=90\n");

    pthread_t tid;
    TC_ASSERT_INT(pthread_create(&tid, NULL, read_option_file_thread, NULL), 0);

    /* 启动时立即加载一次 */
    TC_ASSERT_INT(wait_for_generation(1), 1);
    TC_ASSERT_INT(option_value("CHARGE_STOP"), 90);

    /* 修改文件后由 inotify 触发重新加载 */
    tc_write_file(tc_option_file, "CHARGE_STOP=85\n");
    TC_ASSERT_INT(wait_for_generation(2), 1);
    TC_ASSERT_INT(option_value("CHARGE_STOP"), 85);

    /* 目录被删除后重新监听失败，线程自行退出 */
    unlink(tc_option_file);
    rmdir(tc_option_dir);

    void *ret = (void *)1;
    TC_ASSERT_INT(pthread_join(tid, &ret), 0);
    TC_ASSERT(ret == NULL);

    reset_options();
}

static void test_watch_thread_starts_without_option_file(void)
{
    reset_options();
    set_option_paths("opt_watch_missing");

    pthread_t tid;
    TC_ASSERT_INT(pthread_create(&tid, NULL, read_option_file_thread, NULL), 0);

    /* 文件不存在时使用内置默认值，随后创建文件仍能被监听到 */
    usleep(200 * 1000);
    TC_ASSERT_INT(read_option_generation(), 0);
    TC_ASSERT_INT(option_value("CHARGE_STOP"), 95);

    tc_write_file(tc_option_file, "CHARGE_STOP=60\n");
    TC_ASSERT_INT(wait_for_generation(1), 1);
    TC_ASSERT_INT(option_value("CHARGE_STOP"), 60);

    /* 文件被删除后事件仍会触发，但不应重新加载 */
    unlink(tc_option_file);
    usleep(200 * 1000);
    TC_ASSERT_INT(read_option_generation(), 1);

    rmdir(tc_option_dir);
    TC_ASSERT_INT(pthread_join(tid, NULL), 0);

    reset_options();
}

static void test_watch_thread_fails_on_bad_dir(void)
{
    reset_options();

    snprintf(tc_option_dir, sizeof(tc_option_dir), "%s", tc_path("no_such_watch_dir"));
    snprintf(tc_option_file, sizeof(tc_option_file), "%s/%s", tc_option_dir, option_name);

    /* 监听目录不存在时线程立即返回 */
    TC_ASSERT(read_option_file_thread(NULL) == NULL);
    TC_ASSERT_INT(read_option_generation(), 0);

    reset_options();
}

int main(void)
{
    TC_RUN(test_option_index);
    TC_RUN(test_read_one_option);
    TC_RUN(test_read_one_option_unknown_exits);
    TC_RUN(test_read_option_generation);
    TC_RUN(test_load_missing_file_keeps_defaults);
    TC_RUN(test_load_parses_values);
    TC_RUN(test_load_ignores_comments_and_junk);
    TC_RUN(test_load_rejects_zero_cycle_time);
    TC_RUN(test_load_clamps_meizu_values);
    TC_RUN(test_load_normalizes_mount_modes);
    TC_RUN(test_load_increments_generation);
    TC_RUN(test_is_option_event);
    TC_RUN(test_watch_thread_reloads_on_change);
    TC_RUN(test_watch_thread_starts_without_option_file);
    TC_RUN(test_watch_thread_fails_on_bad_dir);

    return tc_report("read_option_file");
}
