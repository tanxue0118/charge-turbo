#ifndef TURBO_CHARGE_FILE_UTILS_H
#define TURBO_CHARGE_FILE_UTILS_H

#include <stddef.h>

void log_io_failure(const char *action, const char *path, int errnum);
void clear_io_failure(const char *action, const char *path);
void line_feed(char *line);
int file_exists(const char *file);
int file_readable(const char *file);
int ensure_readable(const char *file);
int read_file(const char *file_path, char *buf, size_t buf_size);
int ensure_dir(const char *dir);
void resolve_mount_target(const char *path, char *out, size_t out_size);
int run_shell_command(const char *cmd);
void free_string_array(char ***arr, int num);
int list_dir(const char *path, char ***out);
int parse_non_negative_int(const char *str, int *out);
int contains_ignore_case(const char *s, const char *sub);
int ends_with(const char *s, const char *suffix);
int write_text_file(const char *path, const char *text);
int bind_mount_file(const char *fake, const char *target);
int unbind_mount_file(const char *target);

#endif
