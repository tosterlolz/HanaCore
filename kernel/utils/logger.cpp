#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include "../drivers/screen.hpp"
#include "../libs/libc.h"

extern "C" void print(const char*);

#define ANSI_RESET   "\033[0m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_RED     "\033[31m"
#define ANSI_BLUE    "\033[34m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_GRAY    "\033[90m"

namespace hanacore {
namespace utils {

    static void print_colorv(const char* color, const char* tag, const char* fmt, va_list args) {
        char buf[512];
        size_t pos = 0;

        // kopiujemy tag do buf
        for (const char* p = tag; *p && pos + 1 < sizeof(buf); ++p) buf[pos++] = *p;
        if (pos + 1 < sizeof(buf)) buf[pos++] = ' ';

        // formatujemy resztę wiadomości
        int written = vsnprintf(buf + pos, sizeof(buf) - pos, fmt, args);
        if (written < 0) return;

        print(color);
        print(buf);
        print(ANSI_RESET "\n");
    }

    static void print_color(const char* color, const char* tag, const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        print_colorv(color, tag, fmt, args);
        va_end(args);
    }

    void log_ok(const char* fmt, ...) {
        va_list args; va_start(args, fmt);
        print_colorv(ANSI_GREEN, "[OK]", fmt, args);
        va_end(args);
    }

    void log_fail(const char* fmt, ...) {
        va_list args; va_start(args, fmt);
        print_colorv(ANSI_RED, "[FAIL]", fmt, args);
        va_end(args);
    }

    void log_info(const char* fmt, ...) {
        va_list args; va_start(args, fmt);
        print_colorv(ANSI_CYAN, "[INFO]", fmt, args);
        va_end(args);
    }

    void log_debug(const char* fmt, ...) {
        va_list args; va_start(args, fmt);
        print_colorv(ANSI_GRAY, "[DEBUG]", fmt, args);
        va_end(args);
    }

    void log_hex64(const char* label, uint64_t value) {
        if (!label) label = "";
        char buf[64];
        snprintf(buf, sizeof(buf), "%s0x%016llX", label, value);

        print(ANSI_MAGENTA);
        print(buf);
        print(ANSI_RESET "\n");
    }

} // namespace utils
} // namespace hanacore

extern "C" {
    void log_ok(const char *fmt, ...) {
        va_list args; va_start(args, fmt);
        hanacore::utils::print_colorv(ANSI_GREEN, "[OK]", fmt, args);
        va_end(args);
    }
    void log_fail(const char *fmt, ...) {
        va_list args; va_start(args, fmt);
        hanacore::utils::print_colorv(ANSI_RED, "[FAIL]", fmt, args);
        va_end(args);
    }
    void log_info(const char *fmt, ...) {
        va_list args; va_start(args, fmt);
        hanacore::utils::print_colorv(ANSI_CYAN, "[INFO]", fmt, args);
        va_end(args);
    }
    void log_debug(const char *fmt, ...) {
        va_list args; va_start(args, fmt);
        hanacore::utils::print_colorv(ANSI_GRAY, "[DEBUG]", fmt, args);
        va_end(args);
    }
    void log_hex64(const char *label, uint64_t value) {
        hanacore::utils::log_hex64(label, value);
    }
}

// v-variants that accept va_list. Useful for safe forwarding from C++ wrappers.
extern "C" {
    void log_ok_v(const char *fmt, va_list ap) {
        hanacore::utils::print_colorv(ANSI_GREEN, "[OK]", fmt, ap);
    }
    void log_fail_v(const char *fmt, va_list ap) {
        hanacore::utils::print_colorv(ANSI_RED, "[FAIL]", fmt, ap);
    }
    void log_info_v(const char *fmt, va_list ap) {
        hanacore::utils::print_colorv(ANSI_CYAN, "[INFO]", fmt, ap);
    }
    void log_debug_v(const char *fmt, va_list ap) {
        hanacore::utils::print_colorv(ANSI_GRAY, "[DEBUG]", fmt, ap);
    }
}
