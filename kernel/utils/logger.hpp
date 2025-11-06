#pragma once
#include <stdint.h>

// Simple kernel logger helpers. These use the kernel `print()` function
// which is provided by the screen driver (Flanterm or debug port fallback).
extern "C" void log_ok(const char *msg);
extern "C" void log_fail(const char *msg);
extern "C" void log_info(const char *msg);
extern "C" void log_hex64(const char *label, uint64_t value);
