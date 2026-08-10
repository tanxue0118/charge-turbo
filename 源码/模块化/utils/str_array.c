#define _GNU_SOURCE

#include "str_array.h"

#include <stdlib.h>
#include <string.h>

#define STR_ARRAY_INITIAL_CAP 16

void str_array_init(StrArray *arr)
{
    if (!arr) return;

    arr->items = NULL;
    arr->count = 0;
    arr->cap = 0;
}

int str_array_push(StrArray *arr, const char *value)
{
    if (!arr || !value) return 0;

    if (arr->count >= arr->cap) {
        int cap = arr->cap > 0 ? arr->cap * 2 : STR_ARRAY_INITIAL_CAP;
        char **tmp = realloc(arr->items, sizeof(char *) * cap);
        if (!tmp) return 0;

        arr->items = tmp;
        arr->cap = cap;
    }

    char *copy = strdup(value);
    if (!copy) return 0;

    arr->items[arr->count] = copy;
    arr->count++;

    return 1;
}

int str_array_take(StrArray *arr, char ***out)
{
    if (!arr || !out) return 0;

    if (arr->count == 0) {
        free(arr->items);
        str_array_init(arr);
        *out = NULL;
        return 0;
    }

    char **tmp = realloc(arr->items, sizeof(char *) * arr->count);
    if (tmp) arr->items = tmp;

    int count = arr->count;
    *out = arr->items;
    str_array_init(arr);

    return count;
}

void free_string_array(char ***arr, int num)
{
    if (!arr || !*arr) return;

    for (int i = 0; i < num; i++) {
        free((*arr)[i]);
        (*arr)[i] = NULL;
    }

    free(*arr);
    *arr = NULL;
}
