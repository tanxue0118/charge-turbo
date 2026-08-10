#ifndef TURBO_CHARGE_TC_TEST_H
#define TURBO_CHARGE_TC_TEST_H

/* 极简单元测试框架：无外部依赖，仅使用 C 标准库。 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TC_UNUSED __attribute__((unused))

TC_UNUSED static int tc_tests_run = 0;
TC_UNUSED static int tc_tests_failed = 0;
TC_UNUSED static int tc_current_failed = 0;

#define TC_FAIL(fmt, ...)                                                    \
    do {                                                                     \
        tc_current_failed++;                                                 \
        printf("    FAIL %s:%d: " fmt "\n", __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)

#define TC_ASSERT(cond)                                                      \
    do {                                                                     \
        if (!(cond)) TC_FAIL("assertion failed: %s", #cond);                 \
    } while (0)

#define TC_ASSERT_INT(actual, expected)                                      \
    do {                                                                     \
        long tc_a = (long)(actual);                                          \
        long tc_e = (long)(expected);                                        \
        if (tc_a != tc_e)                                                    \
            TC_FAIL("%s == %ld, expected %ld", #actual, tc_a, tc_e);         \
    } while (0)

#define TC_ASSERT_STR(actual, expected)                                      \
    do {                                                                     \
        const char *tc_a = (actual);                                         \
        const char *tc_e = (expected);                                       \
        if (!tc_a || !tc_e || strcmp(tc_a, tc_e) != 0)                       \
            TC_FAIL("%s == \"%s\", expected \"%s\"",                         \
                    #actual, tc_a ? tc_a : "(null)", tc_e ? tc_e : "(null)"); \
    } while (0)

#define TC_RUN(test_func)                                                    \
    do {                                                                     \
        tc_tests_run++;                                                      \
        tc_current_failed = 0;                                               \
        printf("  %s\n", #test_func);                                        \
        test_func();                                                         \
        if (tc_current_failed) tc_tests_failed++;                            \
    } while (0)

TC_UNUSED static int tc_report(const char *suite)
{
    printf("%s: %d tests, %d failed\n", suite, tc_tests_run, tc_tests_failed);
    return tc_tests_failed == 0 ? 0 : 1;
}

/* 在临时目录中准备测试用文件，测试进程退出时由外部 make 清理。 */
TC_UNUSED static char tc_tmp_root[512];

TC_UNUSED static const char *tc_tmpdir(void)
{
    if (tc_tmp_root[0]) return tc_tmp_root;

    snprintf(tc_tmp_root, sizeof(tc_tmp_root), "/tmp/tc_test_%d", (int)getpid());
    mkdir(tc_tmp_root, 0755);

    return tc_tmp_root;
}

/* 轮换缓冲区，允许在同一个表达式里多次调用 tc_path()。 */
#define TC_PATH_SLOTS 16

TC_UNUSED static const char *tc_path(const char *relative)
{
    static char bufs[TC_PATH_SLOTS][1024];
    static int slot = 0;

    char *buf = bufs[slot];
    slot = (slot + 1) % TC_PATH_SLOTS;

    snprintf(buf, 1024, "%s/%s", tc_tmpdir(), relative);

    return buf;
}

TC_UNUSED static void tc_mkdirs(const char *path)
{
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", path);

    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(buf, 0755);
        *p = '/';
    }

    mkdir(buf, 0755);
}

TC_UNUSED static int tc_write_file(const char *path, const char *content)
{
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);

    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        tc_mkdirs(dir);
    }

    FILE *fp = fopen(path, "w");
    if (!fp) return 0;

    if (content) fputs(content, fp);
    fclose(fp);

    return 1;
}

TC_UNUSED static int tc_read_file(const char *path, char *out, size_t out_size)
{
    if (out_size) out[0] = '\0';

    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    size_t n = fread(out, 1, out_size - 1, fp);
    out[n] = '\0';
    fclose(fp);

    return 1;
}

#endif
