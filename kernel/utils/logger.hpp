#pragma once
#include <stdint.h>

// Simple kernel logger helpers. These use the kernel `print()` function
// which is provided by the screen driver (Flanterm or debug port fallback).
extern "C" void log_ok(const char *msg);
extern "C" void log_fail(const char *msg);
extern "C" void log_info(const char *msg);
extern "C" void log_debug(const char *msg);
extern "C" void log_hex64(const char *label, uint64_t value);

// C++ namespace-friendly wrappers. These call the C ABI functions so both C
// and C++ code can use the same logging API. Keep these inline and freestanding.
namespace hanacore {
	namespace utils {
		inline void log_ok_cpp(const char *msg, ...) { ::log_ok(msg); }
		inline void log_fail_cpp(const char *msg, ...) { ::log_fail(msg); }
		inline void log_info_cpp(const char *msg, ...) { ::log_info(msg); }
		inline void log_debug_cpp(const char *msg, ...) { ::log_debug(msg); }
		inline void log_hex64_cpp(const char *label, uint64_t value) { ::log_hex64(label, value); }
	} // namespace utils
} // namespace hanacore
