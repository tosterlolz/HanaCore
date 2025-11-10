#include "../kernel/api/hanaapi.h"
#include <stdint.h>

/* Minimal hana syscall wrapper using the x86_64 syscall instruction.
   Supports up to 6 arguments. Returns long result. */
long hana_syscall(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    register long r10 asm("r10") = a4;
    register long r8  asm("r8")  = a5;
    register long r9  asm("r9")  = a6;
    register long ret asm("rax");
    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
    return ret;
}

size_t hana_write(hana_fd_t fd, const void *buf, size_t count) {
    /* SYS_WRITE = 1 */
    long r = hana_syscall(1, (long)fd, (long)(uintptr_t)buf, (long)count, 0, 0, 0);
    if (r < 0) return 0;
    return (size_t)r;
}

/* Directory helpers: wrappers around syscalls */
hana_dir_t *hana_opendir(const char *path) {
    /* SYS_OPEN = 2, flags=0, mode=0 */
    long r = hana_syscall(2, (long)(uintptr_t)path, 0, 0, 0, 0, 0);
    return (hana_dir_t*)(uintptr_t)r;
}

hana_dirent *hana_readdir(hana_dir_t *d) {
    /* Custom syscall: read directory entry (not standard Linux) */
    long r = hana_syscall(26, (long)(uintptr_t)d, 0, 0, 0, 0, 0);
    return (hana_dirent*)(uintptr_t)r;
}

int hana_closedir(hana_dir_t *d) {
    /* SYS_CLOSE = 3 */
    long r = hana_syscall(3, (long)(uintptr_t)d, 0, 0, 0, 0, 0);
    return (int)r;
}

void hana_exit(int status) {
    /* SYS_EXIT = 60 */
    hana_syscall(60, (long)status, 0, 0, 0, 0, 0);
    for(;;) __asm__ volatile("hlt");
}

int hana_chdir(const char *path) {
    (void)path;
    /* Not implemented in kernel: return -1 to indicate failure. */
    return -1;
}

/* Small libc compatibility helpers - implement a few common functions so
   user programs can use standard headers like <string.h> and <stdio.h>.
   These are intentionally minimal and not fully featured. */

#include <stdarg.h>

size_t strlen(const char *s) {
    const char *p = s;
    while (p && *p) ++p;
    return (size_t)(p - s);
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char*)s;
    for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)c;
    return s;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char*)a;
    const unsigned char *pb = (const unsigned char*)b;
    for (size_t i = 0; i < n; ++i) {
        if (pa[i] < pb[i]) return -1;
        if (pa[i] > pb[i]) return 1;
    }
    return 0;
}

int strcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a < *b) return -1;
        if (*a > *b) return 1;
        ++a; ++b;
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

/* Minimal number -> string helper */
static void utoa_unsigned(char *buf, unsigned long long v, int base, int lowercase) {
    char tmp[32]; int ti = 0;
    if (v == 0) { tmp[ti++] = '0'; }
    while (v > 0 && ti < (int)sizeof(tmp)-1) {
        int d = (int)(v % base);
        tmp[ti++] = (char)(d < 10 ? ('0' + d) : ((lowercase ? 'a' : 'A') + (d - 10)));
        v /= base;
    }
    // reverse
    int j = 0;
    while (ti > 0) buf[j++] = tmp[--ti];
    buf[j] = '\0';
}

int vsnprintf(char *out, size_t out_size, const char *fmt, va_list ap) {
    if (!out || out_size == 0) return 0;
    size_t pos = 0;
    for (const char *p = fmt; *p && pos + 1 < out_size; ++p) {
        if (*p != '%') { out[pos++] = *p; continue; }
        ++p;
        if (!*p) break;
        if (*p == 's') {
            const char *s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            while (*s && pos + 1 < out_size) out[pos++] = *s++;
        } else if (*p == 'c') {
            char c = (char)va_arg(ap, int);
            out[pos++] = c;
        } else if (*p == 'd' || *p == 'i') {
            long v = va_arg(ap, long);
            if (v < 0) { if (pos + 1 < out_size) out[pos++] = '-'; v = -v; }
            char num[32]; utoa_unsigned(num, (unsigned long long)v, 10, 0);
            const char *ns = num; while (*ns && pos + 1 < out_size) out[pos++] = *ns++;
        } else if (*p == 'u') {
            unsigned long v = va_arg(ap, unsigned long);
            char num[32]; utoa_unsigned(num, (unsigned long long)v, 10, 0);
            const char *ns = num; while (*ns && pos + 1 < out_size) out[pos++] = *ns++;
        } else if (*p == 'x' || *p == 'X' || *p == 'p') {
            unsigned long v = va_arg(ap, unsigned long);
            char num[32]; utoa_unsigned(num, (unsigned long long)v, 16, (*p=='x' || *p=='p'));
            const char *ns = num; while (*ns && pos + 1 < out_size) out[pos++] = *ns++;
        } else if (*p == '%') {
            out[pos++] = '%';
        } else {
            // unsupported specifier - print verbatim
            out[pos++] = '%';
            if (pos + 1 < out_size) out[pos++] = *p;
        }
    }
    out[pos] = '\0';
    return (int)pos;
}

int snprintf(char *out, size_t out_size, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(out, out_size, fmt, ap);
    va_end(ap);
    return r;
}

int printf(const char *fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    hana_write(1, buf, (size_t)n);
    return n;
}

int puts(const char *s) {
    size_t n = strlen(s);
    hana_write(1, s, n);
    hana_write(1, "\n", 1);
    return (int)n + 1;
}

int putchar(int c) {
    char ch = (char)c;
    hana_write(1, &ch, 1);
    return c;
}

