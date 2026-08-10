#define _GNU_SOURCE

#include "global.h"
#include "file_utils.h"
#include "tc_test.h"

#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_line_feed(void)
{
    char crlf[] = "value\r\n";
    line_feed(crlf);
    TC_ASSERT_STR(crlf, "value");

    char lf[] = "value\n";
    line_feed(lf);
    TC_ASSERT_STR(lf, "value");

    char plain[] = "value";
    line_feed(plain);
    TC_ASSERT_STR(plain, "value");

    char embedded[] = "a\nb";
    line_feed(embedded);
    TC_ASSERT_STR(embedded, "a");

    line_feed(NULL);
}

static void test_parse_non_negative_int(void)
{
    int out = -1;

    TC_ASSERT_INT(parse_non_negative_int("0", &out), 1);
    TC_ASSERT_INT(out, 0);

    TC_ASSERT_INT(parse_non_negative_int("52", &out), 1);
    TC_ASSERT_INT(out, 52);

    TC_ASSERT_INT(parse_non_negative_int("0050", &out), 1);
    TC_ASSERT_INT(out, 50);

    out = -1;
    TC_ASSERT_INT(parse_non_negative_int("-1", &out), 0);
    TC_ASSERT_INT(parse_non_negative_int("", &out), 0);
    TC_ASSERT_INT(parse_non_negative_int("12a", &out), 0);
    TC_ASSERT_INT(parse_non_negative_int(" 12", &out), 0);
    TC_ASSERT_INT(parse_non_negative_int("1.5", &out), 0);
    TC_ASSERT_INT(parse_non_negative_int(NULL, &out), 0);
    TC_ASSERT_INT(parse_non_negative_int("12", NULL), 0);
    TC_ASSERT_INT(out, -1);

    /* 超过 INT_MAX 必须拒绝，避免溢出成负电流值 */
    TC_ASSERT_INT(parse_non_negative_int("99999999999999", &out), 0);
    TC_ASSERT_INT(out, -1);

    char int_max[32];
    snprintf(int_max, sizeof(int_max), "%d", INT_MAX);
    TC_ASSERT_INT(parse_non_negative_int(int_max, &out), 1);
    TC_ASSERT_INT(out, INT_MAX);
}

static void test_contains_ignore_case(void)
{
    TC_ASSERT_INT(contains_ignore_case("Battery", "batt"), 1);
    TC_ASSERT_INT(contains_ignore_case("battery", "BATTERY"), 1);
    TC_ASSERT_INT(contains_ignore_case("Fast Bypass Trickle", "bypass"), 1);
    TC_ASSERT_INT(contains_ignore_case("bms-main", "BMS"), 1);
    TC_ASSERT_INT(contains_ignore_case("usb", "battery"), 0);
    TC_ASSERT_INT(contains_ignore_case("bat", "battery"), 0);
    TC_ASSERT_INT(contains_ignore_case("anything", ""), 1);
    TC_ASSERT_INT(contains_ignore_case(NULL, "x"), 0);
    TC_ASSERT_INT(contains_ignore_case("x", NULL), 0);
}

static void test_ends_with(void)
{
    TC_ASSERT_INT(ends_with("/sys/x/constant_charge_current_max",
                            "/constant_charge_current_max"), 1);
    TC_ASSERT_INT(ends_with("/sys/x/input_current_limit", "/input_current_max"), 0);
    TC_ASSERT_INT(ends_with("temp", "temp"), 1);
    TC_ASSERT_INT(ends_with("mp", "temp"), 0);
    TC_ASSERT_INT(ends_with("temp", ""), 1);
    TC_ASSERT_INT(ends_with(NULL, "x"), 0);
    TC_ASSERT_INT(ends_with("x", NULL), 0);
}

static void test_file_exists_and_readable(void)
{
    const char *path = tc_path("exists.txt");
    TC_ASSERT_INT(file_exists(path), 0);

    tc_write_file(path, "1");
    TC_ASSERT_INT(file_exists(path), 1);
    TC_ASSERT_INT(file_readable(path), 1);
    TC_ASSERT_INT(ensure_readable(path), 1);

    /* 不可读的节点会被 ensure_readable 改成 0644 */
    chmod(path, 0000);
    TC_ASSERT_INT(file_readable(path), 0);
    TC_ASSERT_INT(ensure_readable(path), 1);
    TC_ASSERT_INT(file_readable(path), 1);

    TC_ASSERT_INT(ensure_readable(tc_path("missing.txt")), 0);
}

static void test_read_file(void)
{
    char buf[32];

    const char *path = tc_path("read.txt");
    tc_write_file(path, "35000\n");

    TC_ASSERT_INT(read_file(path, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "35000");

    /* 只读取第一行 */
    tc_write_file(path, "first\nsecond\n");
    TC_ASSERT_INT(read_file(path, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "first");

    /* 空文件视为读取失败 */
    tc_write_file(path, "");
    TC_ASSERT_INT(read_file(path, buf, sizeof(buf)), 0);
    TC_ASSERT_STR(buf, "");

    TC_ASSERT_INT(read_file(tc_path("missing.txt"), buf, sizeof(buf)), 0);
    TC_ASSERT_INT(read_file(NULL, buf, sizeof(buf)), 0);
    TC_ASSERT_INT(read_file(path, NULL, sizeof(buf)), 0);
    TC_ASSERT_INT(read_file(path, buf, 0), 0);
}

static void test_write_text_file(void)
{
    char buf[64];
    const char *path = tc_path("write.txt");

    TC_ASSERT_INT(write_text_file(path, "45000"), 1);
    TC_ASSERT_INT(tc_read_file(path, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "45000");

    /* 覆盖写入不会留下旧内容 */
    TC_ASSERT_INT(write_text_file(path, "1"), 1);
    TC_ASSERT_INT(tc_read_file(path, buf, sizeof(buf)), 1);
    TC_ASSERT_STR(buf, "1");

    TC_ASSERT_INT(write_text_file(tc_path("no_such_dir/x.txt"), "1"), 0);
    TC_ASSERT_INT(write_text_file(NULL, "1"), 0);
    TC_ASSERT_INT(write_text_file(path, NULL), 0);
}

static void test_ensure_dir(void)
{
    const char *dir = tc_path("new_dir");

    ensure_dir(dir);
    TC_ASSERT_INT(file_exists(dir), 1);

    struct stat st;
    TC_ASSERT_INT(stat(dir, &st), 0);
    TC_ASSERT_INT(st.st_mode & 0777, 0755);

    /* 重复调用不报错 */
    ensure_dir(dir);
    ensure_dir(NULL);
}

static void test_resolve_mount_target(void)
{
    char out[PATH_MAX];

    const char *real = tc_path("real_node");
    tc_write_file(real, "1");

    const char *link = tc_path("link_node");
    unlink(link);
    TC_ASSERT_INT(symlink(real, link), 0);

    resolve_mount_target(link, out, sizeof(out));
    TC_ASSERT_STR(out, real);

    /* 无法解析时原样返回 */
    const char *missing = tc_path("missing_node");
    resolve_mount_target(missing, out, sizeof(out));
    TC_ASSERT_STR(out, missing);

    out[0] = 'x';
    resolve_mount_target(NULL, out, sizeof(out));
    TC_ASSERT_INT(out[0], 'x');
    resolve_mount_target(missing, out, 0);
}

static void test_run_shell_command(void)
{
    TC_ASSERT_INT(run_shell_command("true"), 0);
    TC_ASSERT_INT(run_shell_command("exit 3"), 3);
    TC_ASSERT_INT(run_shell_command("kill -TERM $$"), 128 + 15);
    TC_ASSERT_INT(run_shell_command(NULL), -1);
    TC_ASSERT_INT(run_shell_command(""), -1);
}

static void test_list_dir(void)
{
    char **list = NULL;
    const char *dir = tc_path("list_dir");

    tc_mkdirs(dir);
    TC_ASSERT_INT(list_dir(dir, &list), 0);
    TC_ASSERT(list == NULL);

    char child_a[1024];
    char child_b[1024];
    snprintf(child_a, sizeof(child_a), "%s/a", dir);
    snprintf(child_b, sizeof(child_b), "%s/b", dir);
    tc_write_file(child_a, "1");
    tc_write_file(child_b, "2");

    int num = list_dir(dir, &list);
    TC_ASSERT_INT(num, 2);
    TC_ASSERT(list != NULL);

    int seen_a = 0;
    int seen_b = 0;
    for (int i = 0; i < num; i++) {
        if (!strcmp(list[i], child_a)) seen_a = 1;
        if (!strcmp(list[i], child_b)) seen_b = 1;
    }
    TC_ASSERT_INT(seen_a, 1);
    TC_ASSERT_INT(seen_b, 1);

    free_string_array(&list, num);
    TC_ASSERT(list == NULL);

    /* "." 与 ".." 不会出现在结果中 */
    TC_ASSERT_INT(list_dir(tc_path("no_such_dir"), &list), 0);
    TC_ASSERT(list == NULL);
    TC_ASSERT_INT(list_dir(NULL, &list), 0);
    TC_ASSERT_INT(list_dir(dir, NULL), 0);
}

static void test_list_dir_growth(void)
{
    char **list = NULL;
    const char *dir = tc_path("big_dir");

    tc_mkdirs(dir);

    for (int i = 0; i < 40; i++) {
        char child[1024];
        snprintf(child, sizeof(child), "%s/file_%d", dir, i);
        tc_write_file(child, "1");
    }

    int num = list_dir(dir, &list);
    TC_ASSERT_INT(num, 40);

    for (int i = 0; i < num; i++) {
        TC_ASSERT(list[i] != NULL);
        TC_ASSERT_INT(strncmp(list[i], dir, strlen(dir)), 0);
    }

    free_string_array(&list, num);
}

static void test_free_string_array_is_null_safe(void)
{
    char **list = NULL;

    free_string_array(NULL, 0);
    free_string_array(&list, 3);
    TC_ASSERT(list == NULL);
}

static void test_bind_mount_without_privileges(void)
{
    const char *fake = tc_path("fake_source");
    const char *target = tc_path("mount_target");

    tc_write_file(fake, "0");
    tc_write_file(target, "1");

    /* 普通用户无法 bind mount：必须返回失败而不是崩溃 */
    TC_ASSERT_INT(bind_mount_file(fake, tc_path("missing_target")), 0);
    TC_ASSERT_INT(bind_mount_file(tc_path("missing_source"), target), 0);
    TC_ASSERT_INT(bind_mount_file(NULL, target), 0);
    TC_ASSERT_INT(bind_mount_file(fake, NULL), 0);

    if (geteuid() != 0) {
        TC_ASSERT_INT(bind_mount_file(fake, target), 0);
    }

    /* 未挂载的路径无需卸载 */
    TC_ASSERT_INT(unbind_mount_file(target), 0);
    TC_ASSERT_INT(unbind_mount_file(NULL), 0);
    TC_ASSERT_INT(unbind_mount_file(""), 0);
}

int main(void)
{
    TC_RUN(test_line_feed);
    TC_RUN(test_parse_non_negative_int);
    TC_RUN(test_contains_ignore_case);
    TC_RUN(test_ends_with);
    TC_RUN(test_file_exists_and_readable);
    TC_RUN(test_read_file);
    TC_RUN(test_write_text_file);
    TC_RUN(test_ensure_dir);
    TC_RUN(test_resolve_mount_target);
    TC_RUN(test_run_shell_command);
    TC_RUN(test_list_dir);
    TC_RUN(test_list_dir_growth);
    TC_RUN(test_free_string_array_is_null_safe);
    TC_RUN(test_bind_mount_without_privileges);

    return tc_report("file_utils");
}
