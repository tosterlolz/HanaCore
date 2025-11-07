#pragma once

#include <stddef.h>
#include <stdint.h>

/* When compiling as C++, prefer the standard headers to provide
 * the correct declarations and avoid conflicting language linkages
 * for functions like strstr. For C builds we expose our own
 * prototypes so C files can use them.
 */
#ifdef __cplusplus
# include <cstring>
# include <cstdlib>
/* Provide a C-linkage declaration for a few C functions that
 * user code may call unqualified (e.g. atoi). Some C++ headers
 * put these names in namespace std only, so expose the C symbol
 * to the global namespace with C linkage so calls like `atoi()`
 * resolve to our implementation when linking.
 */
extern "C" {
int atoi(const char* str);
}
#else
/* Ensure C linkage when included from C files */
# ifdef __cplusplus
extern "C" {
# endif

// ===== Memory functions =====
void* memset(void* s, int c, size_t n);
void* memcpy(void* dst, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);

// ===== String functions =====
size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
const char* strstr(const char* haystack, const char* needle);

// ===== Conversion functions =====
int atoi(const char* str);

# ifdef __cplusplus
}
# endif
#endif

