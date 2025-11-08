#pragma once
#include <stdint.h>
#include <stddef.h>

// Simple FD table and types used by kernel syscalls. This is intentionally
// small and minimal to support BusyBox-style file I/O backed by HanaFS.

enum FDType {
    FD_NONE = 0,
    FD_FILE,
    FD_TTY,
    FD_PIPE_READ,
    FD_PIPE_WRITE,
};

struct FDEntry {
    FDType type;
    char *path; // for FD_FILE, owned copy
    uint8_t *buf; // in-memory buffer for file contents
    size_t len; // length of buf
    size_t pos; // current file offset
    int flags; // open flags
    void *pipe_obj; // placeholder for pipe implementation
};

// Allocate per-task FD table with given size. Returns pointer or NULL.
extern "C" struct FDEntry* fdtable_create(int count);
extern "C" void fdtable_destroy(struct FDEntry* table, int count);
extern "C" int fdtable_alloc_fd(struct FDEntry* table, int count);
extern "C" struct FDEntry* fdtable_get(struct FDEntry* table, int count, int fd);
