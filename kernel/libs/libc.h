#pragma once

#include <stddef.h>
#include <stdint.h>
// Ensure C linkage when included from C++ files
#ifdef __cplusplus
extern "C" {
#endif

// ===== Memory functions =====
void* memset(void* s, int c, size_t n);
void* memcpy(void* dst, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);

// ===== String functions =====
size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
char* strstr(const char* haystack, const char* needle);

// ===== Conversion functions =====
int atoi(const char* str);

#ifdef __cplusplus
}
#endif

