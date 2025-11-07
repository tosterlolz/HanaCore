#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)c;
    return s;
}

void *memcpy(void *dst, const void *src, size_t n) {
    // memcpy does NOT support overlapping regions per C standard.
    // Use memmove if overlap is possible.
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        // forward copy (safe)
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else {
        // copy backwards to handle overlap
        for (size_t i = n; i != 0; --i) d[i-1] = s[i-1];
    }
    return dst;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *a = (const unsigned char*)s1;
    const unsigned char *b = (const unsigned char*)s2;
    for (size_t i = 0; i < n; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) ++p;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    if (n == 0) return 0;
    size_t i = 0;
    while (i < n && a[i] && a[i] == b[i]) { ++i; }
    if (i == n) return 0;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++) != '\0') {}
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    char *d = dst;
    size_t i = 0;
    for (; i < n && src[i]; ++i) d[i] = src[i];
    for (; i < n; ++i) d[i] = '\0';
    return dst;
}

char *strchr(const char *s, int c) {
    char cc = (char)c;
    for (; *s; ++s) if (*s == cc) return (char*)s;
    return NULL;
}

char *strrchr(const char *s, int c) {
    char cc = (char)c;
    const char *last = NULL;
    for (; *s; ++s) if (*s == cc) last = s;
    return (char*)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (*needle == '\0') return (char*)haystack;
    for (const char *h = haystack; *h; ++h) {
        const char *p = h;
        const char *n = needle;
        while (*p && *n && *p == *n) { ++p; ++n; }
        if (*n == '\0') return (char*)h;
    }
    return NULL;
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v') ++s;
    int sign = 1;
    if (*s == '+' || *s == '-') {
        if (*s == '-') sign = -1;
        ++s;
    }
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) base = 16;
        else if (s[0] == '0') base = 8;
        else base = 10;
    }
    if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;

    long acc = 0;
    int any = 0;
    for (;;) {
        int digit;
        char c = *s;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
        else break;
        if (digit >= base) break;
        acc = acc * base + digit;
        any = 1;
        ++s;
    }
    if (endptr) *endptr = (char*)(any ? s : nptr);
    return sign * acc;
}

int atoi(const char* str) {
    return (int)strtol(str, NULL, 10);
}

char *utoa(unsigned int value, char *buf, size_t buflen) {
    if (!buf || buflen == 0) return buf;
    // produce digits to a temp buffer reversed
    char tmp[24];
    int pos = 0;
    if (value == 0) tmp[pos++] = '0';
    while (value > 0 && pos < (int)sizeof(tmp)-1) {
        tmp[pos++] = '0' + (value % 10);
        value /= 10;
    }
    // now reverse into buf
    size_t out_len = (size_t)pos;
    if (out_len + 1 > buflen) {
        // not enough space: truncate (keep least significant digits)
        size_t start = out_len - (buflen - 1);
        size_t j = 0;
        for (size_t i = out_len; i > start; --i) {
            buf[j++] = tmp[i-1];
        }
        buf[j] = '\0';
        return buf;
    }
    for (int i = 0; i < pos; ++i) buf[i] = tmp[pos-1-i];
    buf[pos] = '\0';
    return buf;
}

#ifdef __cplusplus
} // extern "C"
#endif