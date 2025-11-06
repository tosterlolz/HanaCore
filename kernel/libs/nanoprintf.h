#pragma once
#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int npf_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int npf_snprintf(char *buf, size_t size, const char *fmt, ...);
void nano_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
