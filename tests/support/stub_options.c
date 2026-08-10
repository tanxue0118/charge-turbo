#define _GNU_SOURCE

#include "global.h"
#include "read_option_file.h"
#include "stub_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long stub_generation = 0;

static int option_slot(const char *name)
{
    for (int i = 0; i < option_count; i++) {
        if (strcmp(options[i].name, name) == 0) return i;
    }

    return -1;
}

void stub_options_reset(void)
{
    for (int i = 0; i < option_count; i++) {
        options[i].value = options[i].default_value;
    }

    stub_generation = 0;
}

void stub_options_set(const char *name, int value)
{
    int idx = option_slot(name);

    if (idx < 0) {
        fprintf(stderr, "stub_options_set: unknown option %s\n", name);
        abort();
    }

    options[idx].value = value;
}

int stub_options_get(const char *name)
{
    int idx = option_slot(name);

    return idx < 0 ? -1 : options[idx].value;
}

void stub_options_set_generation(unsigned long generation)
{
    stub_generation = generation;
}

int read_one_option(const char *name)
{
    int idx = option_slot(name);

    if (idx < 0) {
        fprintf(stderr, "read_one_option: unknown option %s\n", name);
        abort();
    }

    return options[idx].value;
}

unsigned long read_option_generation(void)
{
    return stub_generation;
}

void *read_option_file_thread(void *arg)
{
    (void)arg;

    return NULL;
}
