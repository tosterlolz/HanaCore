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
    (void)fd; (void)count; // current kernel write syscall ignores fd/len and expects NUL-terminated string
    long r = hana_syscall(1, (long)(uintptr_t)buf, 0, 0, 0, 0, 0);
    if (r < 0) return 0;
    return (size_t)r;
}

/* Directory helpers: wrappers around new syscalls */
hana_dir_t *hana_opendir(const char *path) {
    long r = hana_syscall(25, (long)(uintptr_t)path, 0, 0, 0, 0, 0);
    return (hana_dir_t*)(uintptr_t)r;
}

hana_dirent *hana_readdir(hana_dir_t *d) {
    long r = hana_syscall(26, (long)(uintptr_t)d, 0, 0, 0, 0, 0);
    return (hana_dirent*)(uintptr_t)r;
}

int hana_closedir(hana_dir_t *d) {
    long r = hana_syscall(27, (long)(uintptr_t)d, 0, 0, 0, 0, 0);
    return (int)r;
}

void hana_exit(int status) {
    /* syscall number 2 -> SYSCALL_EXIT */
    hana_syscall(2, (long)status, 0, 0, 0, 0, 0);
    for(;;) __asm__ volatile("hlt");
}

int hana_chdir(const char *path) {
    (void)path;
    /* Not implemented in kernel: return -1 to indicate failure. */
    return -1;
}
