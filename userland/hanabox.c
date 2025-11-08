// Stub for stack protector failure (needed for freestanding builds)
void __stack_chk_fail(void) {
    for (;;) {}
}
#include "../kernel/api/hanaapi.h"
#include <stdint.h>

/* Freestanding build: provide minimal string helpers (no libc headers) */
static size_t _strlen(const char *s) { const char *p = s; while (*p) ++p; return (size_t)(p - s); }
static int _strcmp(const char *a, const char *b) { while (*a && *b && *a == *b) { ++a; ++b; } return (unsigned char)*a - (unsigned char)*b; }
static void _strcpy(char *dst, const char *src) { while ((*dst++ = *src++)); }

/* Minimal BusyBox-like single-binary with a few builtins for HanaCore.
 * Supports: echo, cat, rm, mkdir, rmdir, touch, help
 */

/* syscall numbers mirrored from kernel (local userland copy) */
#define HANA_SYSCALL_READ 10
#define HANA_SYSCALL_WRITE 11
#define HANA_SYSCALL_OPEN 12
#define HANA_SYSCALL_CLOSE 13
#define HANA_SYSCALL_LSEEK 14
#define HANA_SYSCALL_UNLINK 15
#define HANA_SYSCALL_MKDIR 16
#define HANA_SYSCALL_RMDIR 17
#define HANA_SYSCALL_STAT 18
#define HANA_SYSCALL_SPAWN 19
#define HANA_SYSCALL_WAITPID 20

extern long hana_syscall(long num, long a1, long a2, long a3, long a4, long a5, long a6);
extern size_t hana_write(hana_fd_t fd, const void *buf, size_t count);
extern void hana_exit(int status) __attribute__((noreturn));

static int my_write(int fd, const void *buf, size_t count) {
    long r = hana_syscall(HANA_SYSCALL_WRITE, (long)fd, (long)(uintptr_t)buf, (long)count, 0, 0, 0);
    return (r < 0) ? -1 : (int)r;
}

static int my_open(const char *path, int flags) {
    long r = hana_syscall(HANA_SYSCALL_OPEN, (long)(uintptr_t)path, (long)flags, 0, 0, 0, 0);
    return (int)r;
}

static int my_close(int fd) {
    long r = hana_syscall(HANA_SYSCALL_CLOSE, (long)fd, 0, 0, 0, 0, 0);
    return (int)r;
}

static int cmd_echo(int argc, char **argv) {
    for (int i = 0; i < argc; ++i) {
        if (i) my_write(1, " ", 1);
        my_write(1, argv[i], _strlen(argv[i]));
    }
    my_write(1, "\n", 1);
    return 0;
}

static int cmd_cat(int argc, char **argv) {
    if (argc == 0) return 0;
    for (int i = 0; i < argc; ++i) {
        const char *path = argv[i];
        int fd = my_open(path, HANA_O_RDONLY);
        if (fd < 0) {
            const char *err = "cat: open failed\n";
            my_write(1, err, _strlen(err));
            continue;
        }
        char buf[256];
        while (1) {
            long got = hana_syscall(HANA_SYSCALL_READ, fd, (long)(uintptr_t)buf, (long)sizeof(buf), 0, 0, 0);
            if (got <= 0) break;
            my_write(1, buf, (size_t)got);
        }
        my_close(fd);
    }
    return 0;
}

static int cmd_rm(int argc, char **argv) {
    for (int i = 0; i < argc; ++i) {
        const char *path = argv[i];
        hana_syscall(HANA_SYSCALL_UNLINK, (long)(uintptr_t)path, 0, 0, 0, 0, 0);
    }
    return 0;
}

static int cmd_mkdir(int argc, char **argv) {
    for (int i = 0; i < argc; ++i) {
        const char *path = argv[i];
        hana_syscall(HANA_SYSCALL_MKDIR, (long)(uintptr_t)path, 0, 0, 0, 0, 0);
    }
    return 0;
}

static int cmd_rmdir(int argc, char **argv) {
    for (int i = 0; i < argc; ++i) {
        const char *path = argv[i];
        hana_syscall(HANA_SYSCALL_RMDIR, (long)(uintptr_t)path, 0, 0, 0, 0, 0);
    }
    return 0;
}

static int cmd_touch(int argc, char **argv) {
    for (int i = 0; i < argc; ++i) {
        const char *path = argv[i];
        int fd = my_open(path, HANA_O_CREAT | HANA_O_WRONLY);
        if (fd >= 0) my_close(fd);
    }
    return 0;
}

static void help(void) {
    const char *msg = "hanabox: supported commands: echo, cat, rm, mkdir, rmdir, touch, help\n";
    my_write(1, msg, _strlen(msg));
}

int main(int argc, char **argv) {
    if (argc < 2) { help(); return 0; }
    const char *cmd = argv[1];
    char **args = &argv[2];
    int nargs = argc - 2;
    if (_strcmp(cmd, "echo") == 0) return cmd_echo(nargs, args);
    if (_strcmp(cmd, "cat") == 0) return cmd_cat(nargs, args);
    if (_strcmp(cmd, "rm") == 0) return cmd_rm(nargs, args);
    if (_strcmp(cmd, "mkdir") == 0) return cmd_mkdir(nargs, args);
    if (_strcmp(cmd, "rmdir") == 0) return cmd_rmdir(nargs, args);
    if (_strcmp(cmd, "touch") == 0) return cmd_touch(nargs, args);
    if (_strcmp(cmd, "help") == 0) { help(); return 0; }
    const char *err = "hanabox: unknown command\n";
    my_write(1, err, _strlen(err));
    return 1;
}
