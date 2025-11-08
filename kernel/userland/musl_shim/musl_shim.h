// Minimal musl syscall shim header
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Provide a fallback 'syscall' symbol that musl will call. This shim maps a
// small subset of Linux SYS_* numbers to Hana's syscall numbers and then
// invokes the syscall instruction with the mapped number.

long syscall(long num, ...);

#ifdef __cplusplus
}
#endif
