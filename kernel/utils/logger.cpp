#include "logger.hpp"
#include <stddef.h>
#include "../drivers/screen.hpp"

// Use the kernel-provided print() to emit log lines. print() is extern "C"
// and will print via Flanterm if initialised, otherwise the debug port.
extern "C" void print(const char*);
namespace hanacore {
    namespace utils {
        // Core implementations inside the hanacore::utils namespace.
        void log_ok(const char *msg) {
            print("[OK] ");
            print(msg);
            print("\n");
        }

        void log_fail(const char *msg) {
            print("[FAIL] ");
            print(msg);
            print("\n");
        }

        void log_info(const char *msg) {
            print("[INFO] ");
            print(msg);
            print("\n");
        }

        void log_debug(const char *msg) {
            print("[DEBUG] ");
            print(msg);
            print("\n");
        }

        void log_hex64(const char *label, uint64_t value) {
            if (!label) label = "";
            // Small stack buffer for hex conversion: "label0x0123456789ABCDEF\n"
            char buf[64];
            size_t pos = 0;
            // copy label
            for (const char* p = label; *p && pos + 1 < sizeof(buf); ++p) buf[pos++] = *p;
            // append 0x
            if (pos + 2 < sizeof(buf)) { buf[pos++] = '0'; buf[pos++] = 'x'; }
            // hex digits (16 nibbles)
            const char* hex = "0123456789ABCDEF";
            for (int i = 15; i >= 0 && pos + 1 < sizeof(buf); --i) {
                uint8_t nib = (value >> (i * 4)) & 0xF;
                buf[pos++] = hex[nib];
            }
            if (pos < sizeof(buf)) buf[pos++] = '\n';
            // NUL-terminate for safety
            if (pos < sizeof(buf)) buf[pos] = '\0'; else buf[sizeof(buf)-1] = '\0';
            print(buf);
        }
    } // namespace utils
} // namespace hanacore

// C wrappers call into the namespaced implementations so existing C ABI
// symbols remain available to the rest of the freestanding kernel.
extern "C" void log_ok(const char *msg) { hanacore::utils::log_ok(msg); }
extern "C" void log_fail(const char *msg) { hanacore::utils::log_fail(msg); }
extern "C" void log_info(const char *msg) { hanacore::utils::log_info(msg); }
extern "C" void log_hex64(const char *label, uint64_t value) { hanacore::utils::log_hex64(label, value); }
