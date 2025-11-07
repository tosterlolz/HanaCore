#include "logger.hpp"
#include <stddef.h>
#include "../drivers/screen.hpp"

extern "C" void print(const char*);

// ANSI color escape sequences (works in QEMU serial & Flanterm)
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

    static inline void print_color(const char* color, const char* tag, const char* msg) {
        print(color);
        print(tag);
        print(ANSI_RESET " ");
        print(msg);
        print("\n");
    }

    void log_ok(const char *msg) {
        print_color(ANSI_GREEN, "[OK]", msg);
    }

    void log_fail(const char *msg) {
        print_color(ANSI_RED, "[FAIL]", msg);
    }

    void log_info(const char *msg) {
        print_color(ANSI_CYAN, "[INFO]", msg);
    }

    void log_debug(const char *msg) {
        print_color(ANSI_GRAY, "[DEBUG]", msg);
    }

    void log_hex64(const char *label, uint64_t value) {
        if (!label) label = "";
        char buf[64];
        size_t pos = 0;
        for (const char* p = label; *p && pos + 1 < sizeof(buf); ++p) buf[pos++] = *p;
        if (pos + 2 < sizeof(buf)) { buf[pos++] = '0'; buf[pos++] = 'x'; }

        const char* hex = "0123456789ABCDEF";
        for (int i = 15; i >= 0 && pos + 1 < sizeof(buf); --i) {
            uint8_t nib = (value >> (i * 4)) & 0xF;
            buf[pos++] = hex[nib];
        }
        if (pos < sizeof(buf)) buf[pos++] = '\n';
        buf[(pos < sizeof(buf)) ? pos : sizeof(buf)-1] = '\0';

        print(ANSI_MAGENTA);
        print(buf);
        print(ANSI_RESET);
    }

} // namespace utils
} // namespace hanacore

// C ABI wrappers
extern "C" void log_ok(const char *msg)   { hanacore::utils::log_ok(msg); }
extern "C" void log_fail(const char *msg) { hanacore::utils::log_fail(msg); }
extern "C" void log_info(const char *msg) { hanacore::utils::log_info(msg); }
extern "C" void log_debug(const char *msg){ hanacore::utils::log_debug(msg); }
extern "C" void log_hex64(const char *label, uint64_t value) { hanacore::utils::log_hex64(label, value); }
