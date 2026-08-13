#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "tc_test.h"

/* 魅族节点是固定的 /sys 路径，改写宏后包含实现，
 * 使写入落到临时目录，同时覆盖其中的 static 函数。 */
#undef MEIZU_WIRED_LEVEL_PATH
#undef MEIZU_WIRED_LEVEL_LEGACY_PATH
#define MEIZU_WIRED_LEVEL_PATH "/tmp/tc_test_meizu/wired_level"
#define MEIZU_WIRED_LEVEL_LEGACY_PATH "/tmp/tc_test_meizu/wired_level_legacy"

#include "value_set.c"

#include <sys/stat.h>
#include <unistd.h>

static void remove_meizu_nodes(void)
{
    chmod(MEIZU_WIRED_LEVEL_PATH, 0644);
    chmod(MEIZU_WIRED_LEVEL_LEGACY_PATH, 0644);
    unlink(MEIZU_WIRED_LEVEL_PATH);
    unlink(MEIZU_WIRED_LEVEL_LEGACY_PATH);
    tc_mkdirs("/tmp/tc_test_meizu");
}

static void test_set_value_writes_when_changed(void)
{
    char buf[64];
    const char *path = tc_path("cccm");

    tc_write_file(path, "500000");
    set_value(path, "300000");
    TC_ASSERT_INT(tc_read_file(path, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "300000");
}

static void test_set_value_skips_when_unchanged(void)
{
    char buf[64];
    const char *path = tc_path("unchanged");

    tc_write_file(path, "1\n");

    struct stat before;
    TC_ASSERT_INT(stat(path, &before), 0);

    /* 内容一致时不应写入，节点内容（含换行）保持原样 */
    set_value(path, "1");
    TC_ASSERT_INT(tc_read_file(path, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "1\n");

    struct stat after;
    TC_ASSERT_INT(stat(path, &after), 0);
    TC_ASSERT_INT(after.st_size, before.st_size);
}

static void test_set_value_handles_trailing_newline(void)
{
    char buf[64];
    const char *path = tc_path("newline");

    /* 旧值带换行时会被 line_feed 去掉再比较 */
    tc_write_file(path, "45\n");
    set_value(path, "45");
    TC_ASSERT_INT(tc_read_file(path, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "45\n");

    tc_write_file(path, "45\n");
    set_value(path, "50");
    TC_ASSERT_INT(tc_read_file(path, buf, sizeof(buf)), 1);
    TC_ASSERT_INT(strncmp(buf, "50", 2), 0);
}

static void test_set_value_fixes_permission(void)
{
    char buf[64];
    const char *path = tc_path("locked");

    tc_write_file(path, "0");
    chmod(path, 0400);

    /* 只读节点会被 chmod 0644 后再写入 */
    set_value(path, "1");
    TC_ASSERT_INT(tc_read_file(path, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "1");

    chmod(path, 0644);
}

static void test_set_value_ignores_invalid_input(void)
{
    char buf[64];
    const char *path = tc_path("valid");

    tc_write_file(path, "1");

    set_value(NULL, "1");
    set_value(path, NULL);
    set_value(tc_path("missing_node"), "1");

    TC_ASSERT_INT(tc_read_file(path, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "1");
}

static void test_set_array_value(void)
{
    char buf[64];
    char *files[3];

    char a[1024];
    char b[1024];
    snprintf(a, sizeof(a), "%s", tc_path("arr_a"));
    snprintf(b, sizeof(b), "%s", tc_path("arr_b"));

    tc_write_file(a, "0");
    tc_write_file(b, "0");

    files[0] = a;
    files[1] = NULL; /* 空指针元素应被跳过而不崩溃 */
    files[2] = b;

    set_array_value(files, 3, "1");

    TC_ASSERT_INT(tc_read_file(a, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "1");
    TC_ASSERT_INT(tc_read_file(b, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "1");

    set_array_value(NULL, 3, "1");
    set_array_value(files, 0, "1");
    set_array_value(files, -1, "1");
    set_array_value(files, 3, NULL);
}

static void test_meizu_level_write_missing_nodes(void)
{
    remove_meizu_nodes();

    int found = -1;
    int success = -1;

    TC_ASSERT_INT(write_meizu_wired_level_with_echo(5, &found, &success),
                  MEIZU_LEVEL_NODE_MISSING);
    TC_ASSERT_INT(found, 0);
    TC_ASSERT_INT(success, 0);

    /* 不传出参也不应崩溃 */
    TC_ASSERT_INT(write_meizu_wired_level_with_echo(5, NULL, NULL),
                  MEIZU_LEVEL_NODE_MISSING);
}

static void test_meizu_level_write_both_nodes(void)
{
    char buf[64];

    remove_meizu_nodes();
    tc_write_file(MEIZU_WIRED_LEVEL_PATH, "1");
    tc_write_file(MEIZU_WIRED_LEVEL_LEGACY_PATH, "1");

    int found = 0;
    int success = 0;

    TC_ASSERT_INT(write_meizu_wired_level_with_echo(7, &found, &success), 0);
    TC_ASSERT_INT(found, 2);
    TC_ASSERT_INT(success, 2);

    TC_ASSERT_INT(tc_read_file(MEIZU_WIRED_LEVEL_PATH, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "7\n");
    TC_ASSERT_INT(tc_read_file(MEIZU_WIRED_LEVEL_LEGACY_PATH, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "7\n");

    /* 写入后节点被 chmod -w 锁定 */
    TC_ASSERT_INT(access(MEIZU_WIRED_LEVEL_PATH, W_OK), -1);
}

static void test_meizu_level_write_clamps_level(void)
{
    char buf[64];

    remove_meizu_nodes();
    tc_write_file(MEIZU_WIRED_LEVEL_PATH, "1");

    int found = 0;
    int success = 0;

    /* 超范围等级会被夹到合法值再写入 */
    TC_ASSERT_INT(write_meizu_wired_level_with_echo(99, &found, &success), 0);
    TC_ASSERT_INT(found, 1);
    TC_ASSERT_INT(success, 1);
    TC_ASSERT_INT(tc_read_file(MEIZU_WIRED_LEVEL_PATH, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "10\n");

    chmod(MEIZU_WIRED_LEVEL_PATH, 0644);
    TC_ASSERT_INT(write_meizu_wired_level_with_echo(0, &found, &success), 0);
    TC_ASSERT_INT(tc_read_file(MEIZU_WIRED_LEVEL_PATH, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "10\n");
}

static void test_meizu_level_write_only_legacy_node(void)
{
    char buf[64];

    remove_meizu_nodes();
    tc_write_file(MEIZU_WIRED_LEVEL_LEGACY_PATH, "1");

    int found = 0;
    int success = 0;

    TC_ASSERT_INT(write_meizu_wired_level_with_echo(3, &found, &success), 0);
    TC_ASSERT_INT(found, 1);
    TC_ASSERT_INT(success, 1);
    TC_ASSERT_INT(tc_read_file(MEIZU_WIRED_LEVEL_LEGACY_PATH, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "3\n");
}

static void test_meizu_restore_permission(void)
{
    remove_meizu_nodes();

    /* 节点不存在时应报告缺失 */
    TC_ASSERT_INT(restore_meizu_wired_level_permission(), MEIZU_LEVEL_NODE_MISSING);

    tc_write_file(MEIZU_WIRED_LEVEL_PATH, "5");
    chmod(MEIZU_WIRED_LEVEL_PATH, 0444);

    TC_ASSERT_INT(restore_meizu_wired_level_permission(), 0);
    TC_ASSERT_INT(access(MEIZU_WIRED_LEVEL_PATH, W_OK), 0);

    struct stat st;
    TC_ASSERT_INT(stat(MEIZU_WIRED_LEVEL_PATH, &st), 0);
    TC_ASSERT_INT(st.st_mode & 0777, 0777);
}

int main(void)
{
    /* 写入后的 "chmod -w" 在没有指定用户类别时受 umask 影响：
     * umask 非零时它只能清掉部分写位并以非 0 退出，
     * 从而让整次写入被判定为失败。清零 umask 保证测试可复现。 */
    umask(0);

    TC_RUN(test_set_value_writes_when_changed);
    TC_RUN(test_set_value_skips_when_unchanged);
    TC_RUN(test_set_value_handles_trailing_newline);
    TC_RUN(test_set_value_fixes_permission);
    TC_RUN(test_set_value_ignores_invalid_input);
    TC_RUN(test_set_array_value);
    TC_RUN(test_meizu_level_write_missing_nodes);
    TC_RUN(test_meizu_level_write_both_nodes);
    TC_RUN(test_meizu_level_write_clamps_level);
    TC_RUN(test_meizu_level_write_only_legacy_node);
    TC_RUN(test_meizu_restore_permission);

    remove_meizu_nodes();

    return tc_report("value_set");
}
