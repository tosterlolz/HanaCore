// Minimal Linux-like syscall dispatcher
#include "syscalls.hpp"
#include "../drivers/screen.hpp"
#include "../utils/logger.hpp"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "fdtable.hpp"
#include "../filesystem/hanafs.hpp"
#include "../filesystem/vfs.hpp"
#include "../filesystem/ramfs.hpp"
#include "../api/hanaapi.h"
#include "../mem/heap.hpp"
#include "../tty/tty.hpp"
#include "../scheduler/scheduler.hpp"
#include "module_runner.hpp"

#include <sys/types.h>

#ifndef O_CREAT
#define O_CREAT 0x40
#endif
#ifndef O_TRUNC
#define O_TRUNC 0x200
#endif
#ifndef O_APPEND
#define O_APPEND 0x400
#endif
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

static size_t kstrlen(const char *s) {
    const char *p = s;
    while (p && *p) ++p;
    return (size_t)(p - s);
}

// Linux-like syscall numbers
enum LinuxSyscall {
    SYS_READ = 0,
    SYS_WRITE = 1,
    SYS_OPEN = 2,
    SYS_CLOSE = 3,
    SYS_STAT = 4,
    SYS_FSTAT = 5,
    SYS_LSEEK = 8,
    SYS_DUP2 = 33,
    SYS_PIPE = 22,
    SYS_EXIT = 60,
    SYS_FORK = 57,
    SYS_WAITPID = 61,
    SYS_MKDIR = 83,
    SYS_RMDIR = 84,
    SYS_UNLINK = 87,
};

// Simple pipe implementation
struct PipeObj {
    uint8_t *buf;
    size_t cap;
    size_t rpos;
    size_t wpos;
    int refs;
};

extern "C" uint64_t syscall_dispatch(uint64_t num, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    (void)d; (void)e; (void)f;

    hanacore::scheduler::Task* cur = hanacore::scheduler::current_task;
    if (!cur || !cur->fds) return (uint64_t)-1;
    struct FDEntry* tbl = cur->fds;
    int cnt = cur->fd_count;

    switch (num) {
        case SYS_WRITE: {
            int fd = (int)a;
            const char* buf = (const char*)(uintptr_t)b;
            size_t count = (size_t)c;
            struct FDEntry* ent = fdtable_get(tbl, cnt, fd);
            if (!ent) return (uint64_t)-1;

            if (ent->type == FD_TTY) {
                if (buf) tty_write(buf);
                return count;
            } else if (ent->type == FD_PIPE_WRITE) {
                struct PipeObj* p = (struct PipeObj*)ent->pipe_obj;
                if (!p) return (uint64_t)-1;
                size_t written = 0;
                for (size_t i = 0; i < count; ++i) {
                    size_t next = (p->wpos + 1) % p->cap;
                    if (next == p->rpos) break;
                    p->buf[p->wpos] = (uint8_t)buf[i];
                    p->wpos = next;
                    ++written;
                }
                return written;
            } else if (ent->type == FD_FILE) {
                size_t needed = ent->pos + count;
                if (needed > ent->len) {
                    uint8_t* nb = (uint8_t*)hanacore::mem::kmalloc(needed);
                    if (!nb) return (uint64_t)-1;
                    for (size_t i = 0; i < ent->len; ++i) nb[i] = ent->buf[i];
                    for (size_t i = ent->len; i < needed; ++i) nb[i] = 0;
                    if (ent->buf) hanacore::mem::kfree(ent->buf);
                    ent->buf = nb; ent->len = needed;
                }
                for (size_t i = 0; i < count; ++i) ent->buf[ent->pos + i] = (uint8_t)buf[i];
                ent->pos += count;
                return count;
            }
            return (uint64_t)-1;
        }

        case SYS_READ: {
            int fd = (int)a;
            char* buf = (char*)(uintptr_t)b;
            size_t count = (size_t)c;
            struct FDEntry* ent = fdtable_get(tbl, cnt, fd);
            if (!ent) return (uint64_t)-1;

            if (ent->type == FD_FILE) {
                size_t avail = ent->len > ent->pos ? (ent->len - ent->pos) : 0;
                size_t tocopy = avail < count ? avail : count;
                if (tocopy > 0 && buf) {
                    for (size_t i = 0; i < tocopy; ++i) buf[i] = (char)ent->buf[ent->pos + i];
                }
                ent->pos += tocopy;
                return tocopy;
            } else if (ent->type == FD_PIPE_READ) {
                struct PipeObj* p = (struct PipeObj*)ent->pipe_obj;
                if (!p) return (uint64_t)-1;
                size_t available = (p->wpos + p->cap - p->rpos) % p->cap;
                size_t toread = available < count ? available : count;
                for (size_t i = 0; i < toread; ++i) {
                    buf[i] = (char)p->buf[(p->rpos + i) % p->cap];
                }
                p->rpos = (p->rpos + toread) % p->cap;
                return toread;
            }
            return (uint64_t)-1;
        }

        case SYS_OPEN: {
            const char* path = (const char*)(uintptr_t)a;
            int flags = (int)b;
            mode_t mode = (mode_t)c; // ignored for now
            if (!path) return (uint64_t)-1;

            size_t len = 0;
            void* data = hanacore::fs::get_file_alloc(path, &len);
            if (!data && (flags & O_CREAT)) ramfs_create_file(path);

            int fd = fdtable_alloc_fd(tbl, cnt);
            if (fd < 0) { if (data) hanacore::mem::kfree(data); return (uint64_t)-1; }

            struct FDEntry* ent = &tbl[fd];
            ent->type = FD_FILE;
            ent->path = (char*)hanacore::mem::kmalloc(strlen(path)+1);
            if (ent->path) strcpy(ent->path, path);

            if (data && len > 0) { ent->buf = (uint8_t*)data; ent->len = len; } 
            else { ent->buf = NULL; ent->len = 0; if (data) hanacore::mem::kfree(data); }

            if (ent->buf && (flags & O_TRUNC)) { hanacore::mem::kfree(ent->buf); ent->buf=NULL; ent->len=0; }
            ent->pos = (flags & O_APPEND) ? ent->len : 0;
            ent->flags = flags;
            return fd;
        }

        case SYS_CLOSE: {
            int fd = (int)a;
            struct FDEntry* ent = fdtable_get(tbl, cnt, fd);
            if (!ent) return (uint64_t)-1;

            if (ent->type == FD_FILE && ent->path) hanacore::fs::hanafs_write_file(ent->path, ent->buf, ent->len);

            if (ent->path) hanacore::mem::kfree(ent->path);
            if (ent->buf) hanacore::mem::kfree(ent->buf);
            ent->type = FD_NONE; ent->path=NULL; ent->buf=NULL; ent->len=0; ent->pos=0;
            return 0;
        }

        case SYS_LSEEK: {
            int fd = (int)a;
            int64_t offset = (int64_t)b;
            int whence = (int)c;
            struct FDEntry* ent = fdtable_get(tbl, cnt, fd);
            if (!ent || ent->type != FD_FILE) return (uint64_t)-1;

            int64_t newpos = 0;
            if (whence == SEEK_SET) newpos = offset;
            else if (whence == SEEK_CUR) newpos = (int64_t)ent->pos + offset;
            else if (whence == SEEK_END) newpos = (int64_t)ent->len + offset;

            if (newpos < 0) return (uint64_t)-1;
            if ((size_t)newpos > ent->len) newpos = ent->len;
            ent->pos = (size_t)newpos;
            return ent->pos;
        }

        case SYS_DUP2: {
            int oldfd = (int)a;
            int newfd = (int)b;
            if (oldfd == newfd) return newfd;
            struct FDEntry* olde = fdtable_get(tbl, cnt, oldfd);
            struct FDEntry* nde = fdtable_get(tbl, cnt, newfd);
            if (!olde || olde->type==FD_NONE) return -1;
            if (nde && nde->type!=FD_NONE) {
                if (nde->path) hanacore::mem::kfree(nde->path);
                if (nde->buf) hanacore::mem::kfree(nde->buf);
                nde->type = FD_NONE; nde->path=NULL; nde->buf=NULL; nde->len=0; nde->pos=0;
            }
            if (olde->path) {
                nde->path = (char*)hanacore::mem::kmalloc(strlen(olde->path)+1);
                strcpy(nde->path, olde->path);
            }
            if (olde->buf && olde->len>0) {
                nde->buf = (uint8_t*)hanacore::mem::kmalloc(olde->len);
                for (size_t i=0;i<olde->len;i++) nde->buf[i]=olde->buf[i];
                nde->len=olde->len;
            }
            nde->pos=olde->pos; nde->flags=olde->flags; nde->type=olde->type;
            return newfd;
        }

        case SYS_PIPE: {
            int* fds = (int*)(uintptr_t)a;
            if (!fds) return -1;
            int rd = fdtable_alloc_fd(tbl, cnt);
            int wr = fdtable_alloc_fd(tbl, cnt);
            if (rd<0 || wr<0) return -1;
            struct PipeObj* p = (struct PipeObj*)hanacore::mem::kmalloc(sizeof(PipeObj));
            p->cap=4096; p->buf=(uint8_t*)hanacore::mem::kmalloc(p->cap); p->rpos=0; p->wpos=0; p->refs=2;
            struct FDEntry* rde=&tbl[rd]; struct FDEntry* wre=&tbl[wr];
            rde->type=FD_PIPE_READ; rde->pipe_obj=p;
            wre->type=FD_PIPE_WRITE; wre->pipe_obj=p;
            fds[0]=rd; fds[1]=wr;
            return 0;
        }

        case SYS_STAT: {
            const char* path = (const char*)(uintptr_t)a;
            struct hana_stat* st = (struct hana_stat*)(uintptr_t)b;
            if (!path || !st) return -1;
            return ::hanafs_stat(path, st)==0 ? 0 : -1;
        }

        case SYS_FSTAT: {
            int fd=(int)a; struct hana_stat* st=(struct hana_stat*)(uintptr_t)b;
            struct FDEntry* ent = fdtable_get(tbl,cnt,fd);
            if (!ent || !st) return -1;
            if (ent->type==FD_FILE && ent->path) return ::hanafs_stat(ent->path, st)==0 ? 0 : -1;
            memset(st,0,sizeof(*st));
            st->st_size=ent->len;
            st->st_mode=(ent->type==FD_TTY)?0x2000:0;
            return 0;
        }

        case SYS_EXIT: {
            int code = (int)a;
            (void)code;
            log_info("sys_exit");
            for(;;){ asm volatile("cli"); asm volatile("hlt"); }
            return 0;
        }

        case SYS_WAITPID: {
            int pid=(int)a;
            (void)b;
            hanacore::scheduler::Task* t = hanacore::scheduler::find_task_by_pid(pid);
            if(!t) return -1;
            while(t->state!=hanacore::scheduler::TASK_DEAD) hanacore::scheduler::sched_yield();
            return pid;
        }

        case SYS_MKDIR: {
            const char* path=(const char*)(uintptr_t)a;
            return path?hanacore::fs::hanafs_make_dir(path)==0?0:-1:-1;
        }

        case SYS_RMDIR: {
            const char* path=(const char*)(uintptr_t)a;
            return path?hanacore::fs::hanafs_remove_dir(path)==0?0:-1:-1;
        }

        case SYS_UNLINK: {
            const char* path=(const char*)(uintptr_t)a;
            return path?hanacore::fs::hanafs_unlink(path)==0?0:-1:-1;
        }

        default:
            log_info("sys_unknown");
            return (uint64_t)-1;
    }
}
