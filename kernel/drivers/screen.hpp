#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Use the Limine-provided definitions for requests and responses. */
#include "../boot/limine.h"

/*
 * Use the platform va_list provided by the C/C++ runtime.
 * For C++ include <cstdarg>, for C include <stdarg.h>.
 * Do not redefine va_start/va_arg/va_end here — that conflicts
 * with the compiler builtins and breaks C/C++ interop.
 */
#ifdef __cplusplus
# include <cstdarg>
#else
# include <stdarg.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- Terminal / screen functions ---
void clear_screen();
void print(const char* str);
void print_fmt(const char* fmt, ...);

// --- Simple framebuffer / GUI helpers ---
void screen_fill_rect(int x, int y, int w, int h, uint32_t color);
void screen_draw_rect(int x, int y, int w, int h, uint32_t color);

// --- sprintf / vsprintf freestanding ---
int vsprintf(char* buffer, const char* fmt, va_list args);
int sprintf(char* buffer, const char* fmt, ...);

#ifdef __cplusplus
}
#if defined(__cplusplus)
namespace hanacore { namespace drivers { namespace screen {
	// C++ namespace wrappers (same signatures as C API)
	void clear_screen();
	void print(const char* str);
	void print_fmt(const char* fmt, ...);
} } }
#endif
#endif
