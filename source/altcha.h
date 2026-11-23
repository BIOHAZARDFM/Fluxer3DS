#ifndef ALTCHA_H
#define ALTCHA_H
#include <stddef.h>
int altcha_begin(const char *error_json);
int altcha_step(unsigned int budget, char *token, size_t token_size);
unsigned long altcha_progress(void);
void altcha_cancel(void);
#endif
