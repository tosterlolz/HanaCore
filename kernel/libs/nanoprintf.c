/* Very small vsnprintf implementation supporting %s, %d, %u, %x, %p, %c, %% */
#include "nanoprintf.h"
#include "../drivers/screen.hpp"
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>

static void debug_putchar(char c) {
    __asm__ volatile("outb %0, $0xe9" : : "a"(c));
}

static char *uitoa_base(unsigned long long value, char *buf_end, int base, int lowercase) {
    static const char digits_up[] = "0123456789ABCDEF";
    static const char digits_low[] = "0123456789abcdef";
    const char *digits = lowercase ? digits_low : digits_up;
    char *p = buf_end;
    if (value == 0) {
        *--p = '0';
        return p;
    }
    while (value) {
        int d = value % base;
        *--p = digits[d];
        value /= base;
    }
    return p;
}

int npf_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    if (size == 0) return 0;
    char *out = buf;
    char *end = buf + size - 1; /* reserve NUL */

    for (; *fmt && out < end; fmt++) {
        if (*fmt != '%') {
            *out++ = *fmt;
            continue;
        }
        fmt++;
        if (!*fmt) break;
        if (*fmt == '%') {
            *out++ = '%';
            continue;
        }
        switch (*fmt) {
            case 's': {
                const char *s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                while (*s && out < end) *out++ = *s++;
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                if (out < end) *out++ = c;
                break;
            }
            case 'd': {
                int v = va_arg(ap, int);
                unsigned int u = (v < 0) ? -v : v;
                if (v < 0 && out < end) *out++ = '-';
                char tmp[32];
                char *p = uitoa_base(u, tmp + sizeof(tmp), 10, 0);
                while (p < tmp + sizeof(tmp) && out < end) *out++ = *p++;
                break;
            }
            case 'u': {
                unsigned int u = va_arg(ap, unsigned int);
                char tmp[32];
                char *p = uitoa_base(u, tmp + sizeof(tmp), 10, 0);
                while (p < tmp + sizeof(tmp) && out < end) *out++ = *p++;
                break;
            }
            case 'x': {
                unsigned int u = va_arg(ap, unsigned int);
                char tmp[32];
                char *p = uitoa_base(u, tmp + sizeof(tmp), 16, 1);
                while (p < tmp + sizeof(tmp) && out < end) *out++ = *p++;
                break;
            }
            case 'p': {
                void *ptr = va_arg(ap, void*);
                unsigned long long val = (unsigned long long)(uintptr_t)ptr;
                char tmp[32];
                char *p = uitoa_base(val, tmp + sizeof(tmp), 16, 1);
                if (out < end) *out++ = '0';
                if (out < end) *out++ = 'x';
                while (p < tmp + sizeof(tmp) && out < end) *out++ = *p++;
                break;
            }
            default:
                /* unsupported: print placeholder */
                if (out < end) *out++ = '?';
                break;
        }
    }
    *out = '\0';
    return (int)(out - buf);
}

int npf_snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = npf_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

void nano_log(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    npf_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* Send to Flanterm if available */
    print(buf);

    /* Also send to Limine debug port */
    for (char *p = buf; *p; ++p) debug_putchar(*p);
}
