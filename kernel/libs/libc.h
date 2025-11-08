#pragma once

#include <stddef.h>
#include <stdint.h>

/* When compiling as C++, prefer the standard headers to provide
* the correct declarations and avoid conflicting language linkages
 * for functions like strstr. For C builds we expose our own
 * prototypes so C files can use them.
 */
#ifdef __cplusplus
#include <cstring>
#include <cstdlib>
#include <cstdarg>
/* Provide a C-linkage declaration for a few C functions that
 * user code may call unqualified (e.g. atoi). Some C++ headers
 * put these names in namespace std only, so expose the C symbol
 * to the global namespace with C linkage so calls like `atoi()`
 * resolve to our implementation when linking.
 */
extern "C" {
// ===== Memory functions =====
void* memset(void* s, int c, size_t n);
void* memcpy(void* dst, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);

// ===== String functions =====
size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
int vsnprintf(char* str, size_t size, const char* format, va_list ap);
int sprintf(char* str, const char* format, ...);
int snprintf(char* str, size_t size, const char* format, ...);
char* strcat(char* dest, const char* src);
// ===== Conversion functions =====
int atoi(const char* str);

}
#else
int atoi(const char* str);
#endif

