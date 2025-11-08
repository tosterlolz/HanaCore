// Minimal syscall dispatcher implementation.
// This file provides a tiny set of syscalls the kernel exposes to
// userland or to in-kernel userland-like code.

#include "syscalls.hpp"
#include "../drivers/screen.hpp"
#include "../utils/logger.hpp"

#include <stdint.h>
#include <stddef.h>
#include "fdtable.hpp"
#include "../filesystem/hanafs.hpp"
#include "../api/hanaapi.h"
#include "../mem/heap.hpp"
#include "../tty/tty.hpp"
#include "../scheduler/scheduler.hpp"
#include "module_runner.hpp"
#include <string.h>

static size_t kstrlen(const char *s) {
    const char *p = s;
    while (p && *p) ++p;
    return (size_t)(p - s);
}

// Simple pipe implementation used by the minimal syscall layer.
struct PipeObj {
    uint8_t *buf;
    size_t cap;
    size_t rpos;
    size_t wpos;
    int refs;
};

extern "C" uint64_t syscall_dispatch(uint64_t num, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    (void)d;
    (void)e;
    (void)f;

    switch (num) {
        case SYSCALL_WRITE: {
            // a -> const char * (virtual address)
            const char *s = (const char *)(uintptr_t)a;
            if (!s) return 0;
            // Print via the kernel-provided print() front-end.
            print(s);
            return (uint64_t)kstrlen(s);
        }

        case SYSCALL_EXIT: {
            // a -> exit code (ignored for now)
            (void)a;
            log_info("sys_exit");
            // Halt the CPU in a tight loop.
            for (;;) {
                asm volatile ("cli");
                asm volatile ("hlt");
            }
            // Unreachable
            return 0;
        }

        case HANA_SYSCALL_OPEN: {
            // a -> const char* path, b -> flags
            const char* path = (const char*)(uintptr_t)a;
            int flags = (int)b;
            if (!path) return (uint64_t)-1;
            // allocate fd in current task
            // simple: read full file into buffer if exists, else create empty for O_CREAT
            size_t len = 0; void* data = hanacore::fs::hanafs_get_file_alloc(path, &len);
            // find fd using current task
            hanacore::scheduler::Task* cur = hanacore::scheduler::current_task;
            if (!cur || !cur->fds) { if (data) hanacore::mem::kfree(data); return (uint64_t)-1; }
            struct FDEntry* tbl = cur->fds; int cnt = cur->fd_count;
            int fd = fdtable_alloc_fd(tbl, cnt);
            if (fd < 0) { if (data) hanacore::mem::kfree(data); return (uint64_t)-1; }
            struct FDEntry* ent = &tbl[fd];
            ent->type = FD_FILE;
            ent->path = (char*)hanacore::mem::kmalloc(strlen(path) + 1);
            if (ent->path) { strcpy(ent->path, path); }
            if (data && len > 0) {
                ent->buf = (uint8_t*)data;
                ent->len = len;
            } else {
                ent->buf = NULL; ent->len = 0;
                if (data) hanacore::mem::kfree(data);
            }
            ent->pos = 0;
            ent->flags = flags;
            return (uint64_t)fd;
        }

        case HANA_SYSCALL_READ: {
            // a -> fd, b -> buf, c -> count
            int fd = (int)a; char* buf = (char*)(uintptr_t)b; size_t count = (size_t)c;
            hanacore::scheduler::Task* cur = hanacore::scheduler::current_task;
            if (!cur || !cur->fds) return (uint64_t)-1;
            struct FDEntry* tbl = cur->fds; int cnt = cur->fd_count;
            struct FDEntry* ent = fdtable_get(tbl, cnt, fd);
            if (!ent) return (uint64_t)-1;
            if (ent->type == FD_FILE) {
                size_t avail = ent->len > ent->pos ? (ent->len - ent->pos) : 0;
                size_t tocopy = avail < count ? avail : count;
                if (tocopy > 0 && buf) {
                    for (size_t i = 0; i < tocopy; ++i) buf[i] = (char)ent->buf[ent->pos + i];
                }
                ent->pos += tocopy;
                return (uint64_t)tocopy;
            } else if (ent->type == FD_PIPE_READ) {
                // pipe semantics: read from pipe buffer
                struct PipeObj* p = (struct PipeObj*)ent->pipe_obj;
                if (!p) return (uint64_t)-1;
                size_t available = (p->wpos + p->cap - p->rpos) % p->cap;
                size_t toread = available < count ? available : count;
                for (size_t i = 0; i < toread; ++i) {
                    buf[i] = (char)p->buf[(p->rpos + i) % p->cap];
                }
                p->rpos = (p->rpos + toread) % p->cap;
                return (uint64_t)toread;
            }
            return (uint64_t)-1;
        }

        case HANA_SYSCALL_WRITE: {
            // a -> fd, b -> buf, c -> count
            int fd = (int)a; const char* sbuf = (const char*)(uintptr_t)b; size_t count = (size_t)c;
            hanacore::scheduler::Task* cur = hanacore::scheduler::current_task;
            if (!cur || !cur->fds) return (uint64_t)-1;
            struct FDEntry* tbl = cur->fds; int cnt = cur->fd_count;
            struct FDEntry* ent = fdtable_get(tbl, cnt, fd);
            if (!ent) return (uint64_t)-1;
            if (ent->type == FD_TTY) {
                // write to tty
                if (sbuf) tty_write(sbuf);
                return (uint64_t)count;
            }
            if (ent->type == FD_PIPE_WRITE) {
                struct PipeObj* p = (struct PipeObj*)ent->pipe_obj;
                if (!p) return (uint64_t)-1;
                size_t written = 0;
                for (size_t i = 0; i < count; ++i) {
                    size_t next = (p->wpos + 1) % p->cap;
                    if (next == p->rpos) break; // full
                    p->buf[p->wpos] = (uint8_t)sbuf[i];
                    p->wpos = next; ++written;
                }
                return (uint64_t)written;
            }
            if (ent->type != FD_FILE) return (uint64_t)-1;
            // ensure buffer large enough
            size_t needed = ent->pos + count;
            if (needed > ent->len) {
                uint8_t* nb = (uint8_t*)hanacore::mem::kmalloc(needed);
                if (!nb) return (uint64_t)-1;
                for (size_t i = 0; i < ent->len; ++i) nb[i] = ent->buf[i];
                for (size_t i = ent->len; i < needed; ++i) nb[i] = 0;
                if (ent->buf) hanacore::mem::kfree(ent->buf);
                ent->buf = nb; ent->len = needed;
            }
            for (size_t i = 0; i < count; ++i) ent->buf[ent->pos + i] = (uint8_t)sbuf[i];
            ent->pos += count;
            return (uint64_t)count;
        }

        case HANA_SYSCALL_LSEEK: {
            int fd = (int)a; int64_t offset = (int64_t)b; int whence = (int)c;
            hanacore::scheduler::Task* cur = hanacore::scheduler::current_task;
            if (!cur || !cur->fds) return (uint64_t)-1;
            struct FDEntry* tbl = cur->fds; int cnt = cur->fd_count;
            struct FDEntry* ent = fdtable_get(tbl, cnt, fd);
            if (!ent || ent->type != FD_FILE) return (uint64_t)-1;
            int64_t newpos = 0;
            if (whence == HANA_SEEK_SET) newpos = offset;
            else if (whence == HANA_SEEK_CUR) newpos = (int64_t)ent->pos + offset;
            else if (whence == HANA_SEEK_END) newpos = (int64_t)ent->len + offset;
            if (newpos < 0) return (uint64_t)-1;
            if ((size_t)newpos > ent->len) newpos = ent->len;
            ent->pos = (size_t)newpos;
            return (uint64_t)ent->pos;
        }

        case HANA_SYSCALL_STAT: {
            const char* path = (const char*)(uintptr_t)a;
            struct hana_stat* st = (struct hana_stat*)(uintptr_t)b;
            if (!path || !st) return (uint64_t)-1;
            int rc = ::hanafs_stat(path, st);
            return rc == 0 ? 0 : (uint64_t)-1;
        }

        case HANA_SYSCALL_FSTAT: {
            int fd = (int)a;
            struct hana_stat* st = (struct hana_stat*)(uintptr_t)b;
            if (!st) return (uint64_t)-1;
            hanacore::scheduler::Task* cur = hanacore::scheduler::current_task;
            if (!cur || !cur->fds) return (uint64_t)-1;
            struct FDEntry* tbl = cur->fds; int cnt = cur->fd_count;
            struct FDEntry* ent = fdtable_get(tbl, cnt, fd);
            if (!ent) return (uint64_t)-1;
            if (ent->type == FD_FILE && ent->path) {
                return ::hanafs_stat(ent->path, st) == 0 ? 0 : (uint64_t)-1;
            }
            // for pipes/tty, fill minimal info
            memset(st, 0, sizeof(*st));
            st->st_size = ent->len;
            st->st_mode = (ent->type == FD_TTY) ? 0x2000 : 0; // misc
            return 0;
        }

        case HANA_SYSCALL_OPENDIR: {
            const char* path = (const char*)(uintptr_t)a;
            if (!path) return (uint64_t)0;
            struct hana_dir* d = ::hanafs_opendir(path);
            return (uint64_t)(uintptr_t)d;
        }

        case HANA_SYSCALL_READDIR: {
            struct hana_dir* d = (struct hana_dir*)(uintptr_t)a;
            if (!d) return (uint64_t)0;
            struct hana_dirent* ent = ::hanafs_readdir(d);
            return (uint64_t)(uintptr_t)ent;
        }

        case HANA_SYSCALL_CLOSEDIR: {
            struct hana_dir* d = (struct hana_dir*)(uintptr_t)a;
            if (!d) return (uint64_t)-1;
            int rc = ::hanafs_closedir(d);
            return rc == 0 ? 0 : (uint64_t)-1;
        }

        case HANA_SYSCALL_DUP2: {
            int oldfd = (int)a; int newfd = (int)b;
            hanacore::scheduler::Task* cur = hanacore::scheduler::current_task;
            if (!cur || !cur->fds) return (uint64_t)-1;
            struct FDEntry* tbl = cur->fds; int cnt = cur->fd_count;
            if (oldfd < 0 || oldfd >= cnt || newfd < 0 || newfd >= cnt) return (uint64_t)-1;
            if (oldfd == newfd) return (uint64_t)newfd;
            struct FDEntry* olde = fdtable_get(tbl, cnt, oldfd);
            if (!olde || olde->type == FD_NONE) return (uint64_t)-1;
            // if newfd occupied, close it
            struct FDEntry* nde = fdtable_get(tbl, cnt, newfd);
            if (nde && nde->type != FD_NONE) {
                // reuse existing close logic: free resources
                if (nde->path) hanacore::mem::kfree(nde->path);
                if (nde->buf) hanacore::mem::kfree(nde->buf);
                if (nde->pipe_obj) {
                    struct PipeObj* p = (struct PipeObj*)nde->pipe_obj; if (p) { p->refs--; if (p->refs == 0) { if (p->buf) hanacore::mem::kfree(p->buf); hanacore::mem::kfree(p); } }
                }
                nde->type = FD_NONE; nde->path = NULL; nde->buf = NULL; nde->len = 0; nde->pos = 0; nde->flags = 0; nde->pipe_obj = NULL;
            }
            // deep-copy old entry into newfd
            if (olde->path) {
                nde->path = (char*)hanacore::mem::kmalloc(strlen(olde->path) + 1);
                if (nde->path) strcpy(nde->path, olde->path);
            } else nde->path = NULL;
            if (olde->buf && olde->len > 0) {
                nde->buf = (uint8_t*)hanacore::mem::kmalloc(olde->len);
                if (nde->buf) { for (size_t i = 0; i < olde->len; ++i) nde->buf[i] = olde->buf[i]; nde->len = olde->len; }
            } else { nde->buf = NULL; nde->len = 0; }
            nde->pos = olde->pos; nde->flags = olde->flags; nde->type = olde->type;
            if (olde->pipe_obj) {
                struct PipeObj* p = (struct PipeObj*)olde->pipe_obj; if (p) p->refs++;
                nde->pipe_obj = olde->pipe_obj;
            }
            return (uint64_t)newfd;
        }

        case HANA_SYSCALL_PIPE: {
            int *fds = (int*)(uintptr_t)a;
            if (!fds) return (uint64_t)-1;
            hanacore::scheduler::Task* cur = hanacore::scheduler::current_task;
            if (!cur || !cur->fds) return (uint64_t)-1;
            struct FDEntry* tbl = cur->fds; int cnt = cur->fd_count;
            int rd = fdtable_alloc_fd(tbl, cnt);
            int wr = fdtable_alloc_fd(tbl, cnt);
            if (rd < 0 || wr < 0) return (uint64_t)-1;
            struct PipeObj* p = (struct PipeObj*)hanacore::mem::kmalloc(sizeof(struct PipeObj));
            if (!p) return (uint64_t)-1;
            p->cap = 4096; p->buf = (uint8_t*)hanacore::mem::kmalloc(p->cap); p->rpos = 0; p->wpos = 0; p->refs = 2;
            if (!p->buf) { hanacore::mem::kfree(p); return (uint64_t)-1; }
            struct FDEntry* rde = &tbl[rd]; struct FDEntry* wre = &tbl[wr];
            rde->type = FD_PIPE_READ; rde->pipe_obj = p; rde->path = NULL; rde->buf = NULL; rde->len = 0; rde->pos = 0; rde->flags = 0;
            wre->type = FD_PIPE_WRITE; wre->pipe_obj = p; wre->path = NULL; wre->buf = NULL; wre->len = 0; wre->pos = 0; wre->flags = 0;
            // write fds back to user pointer
            fds[0] = rd; fds[1] = wr;
            return 0;
        }

        case HANA_SYSCALL_CLOSE: {
            int fd = (int)a;
            hanacore::scheduler::Task* cur = hanacore::scheduler::current_task;
            if (!cur || !cur->fds) return (uint64_t)-1;
            struct FDEntry* tbl = cur->fds; int cnt = cur->fd_count;
            struct FDEntry* ent = fdtable_get(tbl, cnt, fd);
            if (!ent) return (uint64_t)-1;
            if (ent->type == FD_FILE) {
                // persist changes back to HanaFS if path present
                if (ent->path) {
                    hanacore::fs::hanafs_write_file(ent->path, ent->buf, ent->len);
                }
            }
            if (ent->path) hanacore::mem::kfree(ent->path);
            if (ent->buf) hanacore::mem::kfree(ent->buf);
            ent->type = FD_NONE;
            ent->path = NULL; ent->buf = NULL; ent->len = 0; ent->pos = 0;
            return 0;
        }

        case HANA_SYSCALL_UNLINK: {
            const char* path = (const char*)(uintptr_t)a;
            if (!path) return (uint64_t)-1;
            int rc = hanacore::fs::hanafs_unlink(path);
            return rc == 0 ? 0 : (uint64_t)-1;
        }

        case HANA_SYSCALL_MKDIR: {
            const char* path = (const char*)(uintptr_t)a;
            if (!path) return (uint64_t)-1;
            int rc = hanacore::fs::hanafs_make_dir(path);
            return rc == 0 ? 0 : (uint64_t)-1;
        }

        case HANA_SYSCALL_RMDIR: {
            const char* path = (const char*)(uintptr_t)a;
            if (!path) return (uint64_t)-1;
            int rc = hanacore::fs::hanafs_remove_dir(path);
            return rc == 0 ? 0 : (uint64_t)-1;
        }

        case HANA_SYSCALL_SPAWN: {
            // a -> path (const char*)
            const char* path = (const char*)(uintptr_t)a;
            if (!path) return (uint64_t)-1;
            // create task that calls module runner for path -> exec_module_by_name
            // create a trampoline that captures the path string
            // For simplicity, create a small wrapper that copies path to heap and runs module
            char* pcopy = (char*)hanacore::mem::kmalloc(strlen(path) + 1);
            if (!pcopy) return (uint64_t)-1;
            strcpy(pcopy, path);
            auto entry = (void(*)(void)) + 0; /* placeholder - we will create a lambda-like trampoline not possible in C++ here */
            // As a simple fallback, try running module directly in new task via exec_module_by_name wrapper
            int pid = hanacore::scheduler::create_task((void(*)(void))0);
            (void)pid; (void)pcopy;
            return (uint64_t)-1; // not implemented fully
        }

        case HANA_SYSCALL_WAITPID: {
            // a -> pid, b -> status_ptr (ignored)
            int pid = (int)a;
            // Simple busy-wait loop over tasks; in a proper impl we'd block
            (void)b;
            hanacore::scheduler::Task* t = hanacore::scheduler::find_task_by_pid(pid);
            if (!t) return (uint64_t)-1;
            while (t->state != hanacore::scheduler::TASK_DEAD) {
                hanacore::scheduler::sched_yield();
            }
            return (uint64_t)pid;
        }

        default:
            // Unknown syscall
            log_info("sys_unknown");
            return (uint64_t)-1;
    }
}
