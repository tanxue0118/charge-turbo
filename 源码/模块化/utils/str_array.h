#ifndef TURBO_CHARGE_STR_ARRAY_H
#define TURBO_CHARGE_STR_ARRAY_H

typedef struct {
    char **items;
    int count;
    int cap;
} StrArray;

void str_array_init(StrArray *arr);
int str_array_push(StrArray *arr, const char *value);
int str_array_take(StrArray *arr, char ***out);
void free_string_array(char ***arr, int num);

#endif
