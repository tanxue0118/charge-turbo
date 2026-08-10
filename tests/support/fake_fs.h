#ifndef TURBO_CHARGE_TEST_FAKE_FS_H
#define TURBO_CHARGE_TEST_FAKE_FS_H

#include <stddef.h>

/* file_utils 的内存替身：被测代码里的 /sys 路径由这里模拟。 */

void fake_fs_reset(void);
void fake_fs_add_file(const char *path, const char *content);
void fake_fs_add_dir(const char *path);
void fake_fs_set_unreadable(const char *path);
void fake_fs_set_unwritable(const char *path);
int fake_fs_exists(const char *path);
const char *fake_fs_content(const char *path);

/* set_value / write_text_file 的写入记录 */
int fake_fs_write_count(const char *path);
int fake_fs_total_writes(void);

/* bind_mount_file / unbind_mount_file 的行为控制 */
void fake_fs_set_bind_result(int result);
int fake_fs_mount_count(void);
int fake_fs_umount_count(void);

/* run_shell_command 的返回值与调用记录 */
void fake_fs_set_shell_result(int result);
int fake_fs_shell_count(void);
const char *fake_fs_last_shell_command(void);

#endif
