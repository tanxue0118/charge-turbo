#ifndef TURBO_CHARGE_TEST_STUB_OPTIONS_H
#define TURBO_CHARGE_TEST_STUB_OPTIONS_H

/* read_option_file 的替身：直接操作 global.c 中的选项表，避免读取真实配置文件。 */

void stub_options_reset(void);
void stub_options_set(const char *name, int value);
int stub_options_get(const char *name);
void stub_options_set_generation(unsigned long generation);

#endif
