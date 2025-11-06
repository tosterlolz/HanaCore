#pragma once
#include <stdint.h>
#include <stddef.h>

/* Use the Limine-provided definitions for requests and responses. */
#include "../../boot/limine.h"

#ifdef __cplusplus
extern "C" {
#endif

void clear_screen();
void print(const char* str);

#ifdef __cplusplus
}
#endif
