#include "logger.hpp"
#include <stddef.h>
#include <stdint.h>
#include "../drivers/screen.hpp"

// Use the kernel-provided print() to emit log lines. print() is extern "C"
// and will print via Flanterm if initialised, otherwise the debug port.
extern "C" void print(const char*);

// Small deduplication buffer to avoid printing the same message twice in a row.
static char last_buf[256];

static void emit_with_prefix(const char* prefix, const char* msg) {
    if (!msg) return;
    // Build combined message into a small buffer for deduplication
    char buf[256];
    size_t pos = 0;
    for (const char* p = prefix; *p && pos + 1 < sizeof(buf); ++p) buf[pos++] = *p;
    for (const char* p = msg; *p && pos + 1 < sizeof(buf); ++p) buf[pos++] = *p;
    // ensure NUL
    buf[pos] = '\0';

    // If identical to last printed message, skip to avoid duplicates
    bool same = true;
    for (size_t i = 0; i <= pos && i < sizeof(last_buf); ++i) {
        if (last_buf[i] != buf[i]) { same = false; break; }
        if (buf[i] == '\0') break;
    }
    if (same) return;

    // copy into last_buf
    for (size_t i = 0; i <= pos && i < sizeof(last_buf); ++i) last_buf[i] = buf[i];

    // Emit
    print(prefix);
    print(msg);
    print("\n");
}

extern "C" void log_ok(const char *msg) {
    emit_with_prefix("[OK] ", msg);
}

extern "C" void log_fail(const char *msg) {
    emit_with_prefix("[FAIL] ", msg);
}

extern "C" void log_info(const char *msg) {
    emit_with_prefix("[INFO] ", msg);
}

extern "C" void log_hex64(const char *label, uint64_t value) {
    if (!label) label = "";
    // Small stack buffer for hex conversion: "label0x0123456789ABCDEF\n"
    char buf[128];
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

    emit_with_prefix("", buf);
}
