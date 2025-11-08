#include "fdtable.hpp"
#include "../mem/heap.hpp"
#include <string.h>

extern "C" struct FDEntry* fdtable_create(int count) {
    if (count <= 0) return NULL;
    struct FDEntry* tbl = (struct FDEntry*)hanacore::mem::kmalloc(sizeof(struct FDEntry) * (size_t)count);
    if (!tbl) return NULL;
    for (int i = 0; i < count; ++i) {
        tbl[i].type = FD_NONE;
        tbl[i].path = NULL;
        tbl[i].buf = NULL;
        tbl[i].len = 0;
        tbl[i].pos = 0;
        tbl[i].flags = 0;
        tbl[i].pipe_obj = NULL;
    }
    return tbl;
}

extern "C" void fdtable_destroy(struct FDEntry* table, int count) {
    if (!table) return;
    for (int i = 0; i < count; ++i) {
        if (table[i].path) hanacore::mem::kfree(table[i].path);
        if (table[i].buf) hanacore::mem::kfree(table[i].buf);
        table[i].type = FD_NONE;
    }
    hanacore::mem::kfree(table);
}

extern "C" int fdtable_alloc_fd(struct FDEntry* table, int count) {
    if (!table) return -1;
    for (int i = 3; i < count; ++i) { // reserve 0/1/2 for stdio
        if (table[i].type == FD_NONE) return i;
    }
    return -1;
}

extern "C" struct FDEntry* fdtable_get(struct FDEntry* table, int count, int fd) {
    if (!table) return NULL;
    if (fd < 0 || fd >= count) return NULL;
    return &table[fd];
}
