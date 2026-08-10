#ifndef TURBO_CHARGE_READ_OPTION_FILE_H
#define TURBO_CHARGE_READ_OPTION_FILE_H

unsigned long read_option_generation(void);
int read_one_option(const char *name);
void *read_option_file_thread(void *arg);

#endif
