#pragma once
#include <stdint.h>
#include <stdarg.h>

// Simple kernel logger helpers. These use the kernel `print()` function
// which is provided by the screen driver (Flanterm or debug port fallback).
// Provide both variadic and v-variant (va_list) C ABI functions so callers
// can forward va_list safely from C++ wrappers.
extern "C" void log_ok(const char *fmt, ...);
extern "C" void log_ok_v(const char *fmt, va_list ap);
extern "C" void log_fail(const char *fmt, ...);
extern "C" void log_fail_v(const char *fmt, va_list ap);
extern "C" void log_info(const char *fmt, ...);
extern "C" void log_info_v(const char *fmt, va_list ap);
extern "C" void log_debug(const char *fmt, ...);
extern "C" void log_debug_v(const char *fmt, va_list ap);
extern "C" void log_hex64(const char *label, uint64_t value);

// C++ namespace-friendly wrappers. These call the C ABI v-variants so both C
// and C++ code can use the same logging API while preserving va_list safety.
namespace hanacore {
	namespace utils {
		inline void log_ok_cpp(const char *fmt, ...) {
			va_list ap; va_start(ap, fmt); ::log_ok_v(fmt, ap); va_end(ap);
		}
		inline void log_fail_cpp(const char *fmt, ...) {
			va_list ap; va_start(ap, fmt); ::log_fail_v(fmt, ap); va_end(ap);
		}
		inline void log_info_cpp(const char *fmt, ...) {
			va_list ap; va_start(ap, fmt); ::log_info_v(fmt, ap); va_end(ap);
		}
		inline void log_debug_cpp(const char *fmt, ...) {
			va_list ap; va_start(ap, fmt); ::log_debug_v(fmt, ap); va_end(ap);
		}
		inline void log_hex64_cpp(const char *label, uint64_t value) { ::log_hex64(label, value); }
	} // namespace utils
} // namespace hanacore
