#define _GNU_SOURCE

#include "global.h"
#include "printf_with_time.h"
#include "tc_test.h"

#include <fcntl.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

/* 把 stdout 重定向到临时文件，捕获日志输出后再恢复。 */
static int capture_log(char *out, size_t out_size, const char *format, ...)
{
    const char *path = tc_path("log_capture.txt");

    fflush(stdout);

    int saved = dup(STDOUT_FILENO);
    if (saved < 0) return 0;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        close(saved);
        return 0;
    }

    dup2(fd, STDOUT_FILENO);
    close(fd);

    va_list ap;
    va_start(ap, format);
    char message[LOG_BUF_SIZE];
    vsnprintf(message, sizeof(message), format, ap);
    va_end(ap);

    printf_with_time("%s", message);

    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);

    return tc_read_file(path, out, out_size);
}

static void test_log_line_format(void)
{
    char log[LOG_BUF_SIZE + 128];

    TC_ASSERT_INT(capture_log(log, sizeof(log), "hello"), 1);

    int year = 0;
    int mon = 0;
    int day = 0;
    int hour = 0;
    int min = 0;
    int sec = 0;
    char message[64] = {0};

    int matched = sscanf(log, "[ %4d.%2d.%2dT%2d:%2d:%2d UTC+8 ] %63s",
                         &year, &mon, &day, &hour, &min, &sec, message);
    TC_ASSERT_INT(matched, 7);
    TC_ASSERT_STR(message, "hello");

    TC_ASSERT(year >= 2024);
    TC_ASSERT(mon >= 1 && mon <= 12);
    TC_ASSERT(day >= 1 && day <= 31);
    TC_ASSERT(hour >= 0 && hour <= 23);
    TC_ASSERT(min >= 0 && min <= 59);
    TC_ASSERT(sec >= 0 && sec <= 60);

    /* 恰好一行，以换行结束 */
    TC_ASSERT_INT(log[strlen(log) - 1], '\n');
    TC_ASSERT(strchr(log, '\n') == log + strlen(log) - 1);
}

static void test_log_uses_utc8(void)
{
    char log[LOG_BUF_SIZE + 128];

    TC_ASSERT_INT(capture_log(log, sizeof(log), "x"), 1);

    /* 时间戳应等于 UTC 时间加 8 小时（允许 1 分钟误差） */
    time_t expected = time(NULL) + 8 * 3600;
    struct tm expected_tm;
    gmtime_r(&expected, &expected_tm);

    struct tm logged = {0};
    TC_ASSERT(strptime(log + 2, "%Y.%m.%dT%H:%M:%S", &logged) != NULL);
    logged.tm_isdst = 0;

    time_t logged_time = timegm(&logged);
    long diff = (long)(expected - logged_time);
    if (diff < 0) diff = -diff;
    TC_ASSERT(diff <= 60);
}

static void test_log_formats_arguments(void)
{
    char log[LOG_BUF_SIZE + 128];

    TC_ASSERT_INT(capture_log(log, sizeof(log),
                              "capacity: %d%%, node: %s", 95, "battery"), 1);
    TC_ASSERT(strstr(log, "capacity: 95%, node: battery") != NULL);
}

static void test_log_truncates_long_message(void)
{
    char log[4 * LOG_BUF_SIZE];
    char long_message[LOG_BUF_SIZE * 2];

    memset(long_message, 'a', sizeof(long_message) - 1);
    long_message[sizeof(long_message) - 1] = '\0';

    TC_ASSERT_INT(capture_log(log, sizeof(log), "%s", long_message), 1);

    /* 不应溢出：消息被截断到 LOG_BUF_SIZE - 1 个字符，再加行尾换行 */
    const char *body = strstr(log, "] ");
    TC_ASSERT(body != NULL);
    if (body) TC_ASSERT_INT(strlen(body + 2), LOG_BUF_SIZE);
}

int main(void)
{
    TC_RUN(test_log_line_format);
    TC_RUN(test_log_uses_utc8);
    TC_RUN(test_log_formats_arguments);
    TC_RUN(test_log_truncates_long_message);

    return tc_report("printf_with_time");
}
