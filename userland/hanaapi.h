#ifndef HANA_API_H
#define HANA_API_H

/* HanaCore kernel user API
 * Header to be included by user programs to access kernel services.
 *
 * This file declares a small, portable C API: process control, file I/O,
 * memory management, IPC, timers, basic graphics, and device I/O.
 *
 * The functions are thin ABI-level declarations; implementations are provided
 * by the runtime or the kernel syscall layer.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* Basic types */
typedef int32_t hana_err_t;
typedef uint32_t hana_pid_t;
typedef int32_t hana_fd_t;
typedef uint32_t hana_handle_t;
typedef int64_t hana_off_t;
typedef uint64_t hana_nsec_t;

/* Error handling */
extern int hana_errno; /* set by syscalls on error */
const char *hana_strerror(int err);

/* Exit and process control */
void hana_exit(int status) __attribute__((noreturn));
hana_pid_t hana_spawn(const char *path, const char *const argv[], const char *const envp[]);
/* Replace current image */
int hana_exec(const char *path, const char *const argv[], const char *const envp[]);
/* Wait for child process; returns pid or -1 on error. status is set if non-NULL */
hana_pid_t hana_wait(hana_pid_t pid, int *status);

/* Minimal fork (optional for kernels that support it) */
hana_pid_t hana_fork(void);

/* Signals (small signal API) */
typedef void (*hana_sighandler_t)(int);
int hana_signal(int signum, hana_sighandler_t handler);
int hana_kill(hana_pid_t pid, int signum);

/* Sleep/timers */
void hana_sleep_ms(uint32_t ms);
hana_nsec_t hana_time_now_ns(void);

/* File I/O */
#define HANA_O_RDONLY 0x0000
#define HANA_O_WRONLY 0x0001
#define HANA_O_RDWR   0x0002
#define HANA_O_CREAT  0x0100
#define HANA_O_TRUNC  0x0200
#define HANA_O_APPEND 0x0400

#define HANA_SEEK_SET 0
#define HANA_SEEK_CUR 1
#define HANA_SEEK_END 2

/* File descriptor API */
hana_fd_t hana_open(const char *path, int flags, int mode);
int hana_close(hana_fd_t fd);
size_t hana_read(hana_fd_t fd, void *buf, size_t count);
size_t hana_write(hana_fd_t fd, const void *buf, size_t count);
hana_off_t hana_lseek(hana_fd_t fd, hana_off_t offset, int whence);
int hana_fsync(hana_fd_t fd);

/* Stat */
struct hana_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_size;
    uint64_t st_atime_ns;
    uint64_t st_mtime_ns;
    uint64_t st_ctime_ns;
};
int hana_stat(const char *path, struct hana_stat *st);
int hana_fstat(hana_fd_t fd, struct hana_stat *st);

/* Directory iteration */
typedef struct hana_dirent {
    uint64_t d_ino;
    uint8_t d_type;
    char d_name[256];
} hana_dirent;

typedef struct hana_dir hana_dir_t;
hana_dir_t *hana_opendir(const char *path);
int hana_closedir(hana_dir_t *dir);
hana_dirent *hana_readdir(hana_dir_t *dir);

/* Memory management: mapping and simple heap helpers */
#define HANA_PROT_NONE  0x0
#define HANA_PROT_READ  0x1
#define HANA_PROT_WRITE 0x2
#define HANA_PROT_EXEC  0x4

#define HANA_MAP_SHARED  0x01
#define HANA_MAP_PRIVATE 0x02
#define HANA_MAP_ANONYMOUS 0x10

void *hana_mmap(void *addr, size_t len, int prot, int flags, hana_fd_t fd, hana_off_t offset);
int hana_munmap(void *addr, size_t len);

/* Simple allocator wrappers that map to kernel-managed heap (if provided) */
void *hana_alloc(size_t size);
void hana_free(void *ptr);

/* IPC: channels/messages */
typedef struct {
    uint32_t id;
    uint32_t size;
    uint32_t flags;
} hana_msg_hdr;

hana_handle_t hana_channel_create(void);
int hana_channel_close(hana_handle_t chan);
size_t hana_channel_send(hana_handle_t chan, const void *buf, size_t len, hana_handle_t *handles, size_t nhandles);
size_t hana_channel_recv(hana_handle_t chan, void *buf, size_t len, hana_handle_t *handles, size_t max_handles);

/* Shared ring buffer / event subscription (lightweight) */
typedef struct {
    uint32_t event_id;
    uint32_t flags;
    uint64_t data;
} hana_event_t;

/* Device I/O (ioctl-like interface) */
int hana_device_open(const char *path, hana_handle_t *out_handle);
int hana_device_close(hana_handle_t handle);
int hana_device_ioctl(hana_handle_t handle, uint64_t cmd, void *inbuf, size_t insz, void *outbuf, size_t outsz);

/* Graphics: basic framebuffer access */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t bpp; /* bits per pixel */
    void *framebuffer; /* pointer if mapped into user space */
} hana_fb_info_t;

int hana_graphics_get_info(hana_fd_t fd, hana_fb_info_t *info);
/* Convenience: draw a filled rectangle in ARGB32 in framebuffer (if mapped) */
int hana_graphics_fill_rect(hana_fd_t fd, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t argb);

/* Networking (basic socket-like API) - optional, minimal */
#define HANA_AF_INET 2
#define HANA_SOCK_STREAM 1
#define HANA_SOCK_DGRAM  2

int hana_socket(int domain, int type, int protocol);
int hana_bind(int sockfd, const void *addr, size_t addrlen);
int hana_listen(int sockfd, int backlog);
int hana_accept(int sockfd, void *addr, size_t *addrlen);
int hana_connect(int sockfd, const void *addr, size_t addrlen);
size_t hana_send(int sockfd, const void *buf, size_t len, int flags);
size_t hana_recv(int sockfd, void *buf, size_t len, int flags);
int hana_close_socket(int sockfd);

/* Utilities */
int hana_getcwd(char *buf, size_t size);
int hana_chdir(const char *path);
int hana_unlink(const char *path);
int hana_rename(const char *oldpath, const char *newpath);
int hana_mkdir(const char *path, int mode);
int hana_rmdir(const char *path);

/* Low-level syscall/raw invocation (advanced usage)
 * Provide a raw syscall number + up to 6 args. Returns syscall result or -1.
 * hana_errno is set on error.
 */
long hana_syscall(long num, long a1, long a2, long a3, long a4, long a5, long a6);

/* Convenience macros for compile-time feature checks */
#define HANA_HAVE_SOCKETS 1
#define HANA_HAVE_GRAPHICS 1
#define HANA_HAVE_MMAP 1

#ifdef __cplusplus
}
#endif

#endif /* HANA_API_H */