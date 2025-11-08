// Stub for stack protector failure (needed for freestanding builds)
void __stack_chk_fail(void) {
    // Print error or halt if desired
    for (;;) {}
}
/* Userland shell running via hana syscalls.
 * Simple line-oriented shell that uses hana_spawn/hana_wait for commands
 * and hana_read/hana_write for IO. This is a minimal, robust implementation
 * suitable for the hobby kernel environment.
 */

#include "../hanaapi.h"
#include <stdint.h>
#include <stddef.h>

/* Small userland compatibility fixes: ssize_t */
typedef long ssize_t;

static size_t local_strlen(const char *s) {
    size_t n = 0;
    while (s && *s) { ++n; ++s; }
    return n;
}
static int local_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}
static char *local_strchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; ++s; }
    return NULL;
}
static char *local_strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; ++i) d[i] = s[i];
    for (; i < n; ++i) d[i] = '\0';
    return d;
}
static char *local_strncat(char *d, const char *s, size_t n) {
    size_t l = local_strlen(d);
    size_t i = 0;
    for (; i < n && s[i]; ++i) d[l + i] = s[i];
    d[l + i] = '\0';
    return d;
}
static size_t local_strnlen(const char *s, size_t max) {
    size_t i = 0;
    while (i < max && s[i]) ++i;
    return i;
}

/* Map common names used below to the local implementations */
#define strlen local_strlen
#define strcmp local_strcmp
#define strchr local_strchr
#define strncpy local_strncpy
#define strncat local_strncat
#define strnlen local_strnlen

/* Local thin wrappers for syscalls that may be missing from libhana.c */
static long sys(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    return hana_syscall(num, a1, a2, a3, a4, a5, a6);
}

static ssize_t my_read(int fd, void *buf, size_t cnt) {
    return (ssize_t)sys(10, fd, (long)(uintptr_t)buf, (long)cnt, 0,0,0);
}

static ssize_t my_write(int fd, const void *buf, size_t cnt) {
    return (ssize_t)sys(11, fd, (long)(uintptr_t)buf, (long)cnt, 0,0,0);
}

static hana_pid_t my_spawn(const char *path, const char *const argv[]) {
    return (hana_pid_t)sys(19, (long)(uintptr_t)path, (long)(uintptr_t)argv, 0,0,0,0);
}

static hana_pid_t my_wait(hana_pid_t pid, int *status) {
    return (hana_pid_t)sys(20, pid, (long)(uintptr_t)status, 0,0,0,0);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    char buf[256];
    char out[512];
    for (;;) {
        /* Prompt */
        const char *p = "hcsh$ ";
        my_write(1, p, strlen(p));

        ssize_t r = my_read(0, buf, sizeof(buf)-1);
        if (r <= 0) continue;
        if ((size_t)r >= sizeof(buf)) r = sizeof(buf)-1;
        buf[r] = '\0';
        /* strip newline */
        if (r > 0 && (buf[r-1] == '\n' || buf[r-1] == '\r')) buf[--r] = '\0';

        /* simple tokenization (max 16 args) */
        const int MAXARGS = 16;
        const char *argvs[MAXARGS+1];
        int argcnt = 0;
        char *pcur = buf;
        while (*pcur && argcnt < MAXARGS) {
            while (*pcur == ' ') ++pcur;
            if (!*pcur) break;
            argvs[argcnt++] = pcur;
            while (*pcur && *pcur != ' ') ++pcur;
            if (*pcur == ' ') { *pcur = '\0'; ++pcur; }
        }
        argvs[argcnt] = NULL;
        if (argcnt == 0) continue;

        if (strcmp(argvs[0], "exit") == 0) {
            hana_exit(0);
        }
        if (strcmp(argvs[0], "cd") == 0) {
            if (argcnt >= 2) hana_chdir(argvs[1]);
            continue;
        }

        /* Build command path: if contains '/', use as-is, else prefix /bin/ */
        char pathbuf[256];
        if (strchr(argvs[0], '/')) {
            strncpy(pathbuf, argvs[0], sizeof(pathbuf)-1); pathbuf[sizeof(pathbuf)-1]=0;
        } else {
            /* Build /bin/<cmd> without relying on snprintf/stdio */
            strncpy(pathbuf, "/bin/", sizeof(pathbuf)-1);
            pathbuf[sizeof(pathbuf)-1] = '\0';
            /* safe concat */
            size_t remain = sizeof(pathbuf) - strlen(pathbuf) - 1;
            if (remain > 0) {
                strncat(pathbuf, argvs[0], remain);
            }
        }

        hana_pid_t pid = my_spawn(pathbuf, argvs);
        if (pid <= 0) {
            /* Avoid snprintf - assemble message with writes */
            const char *msg = "hcsh: failed to spawn ";
            my_write(1, msg, strlen(msg));
            my_write(1, pathbuf, strnlen(pathbuf, sizeof(pathbuf)));
            my_write(1, "\n", 1);
            continue;
        }
        int status = 0;
        my_wait(pid, &status);
    }

    return 0;
}
